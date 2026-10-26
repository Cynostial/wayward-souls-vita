#include "ws_linker.h"

static int same(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static const char *basename_of(const char *name) {
    const char *base = name;
    if (!name) return 0;
    for (; *name; ++name) if (*name == '/' || *name == '\\') base = name + 1;
    return base;
}
int ws_module_index(const ws_link_context *ctx, const char *needed) {
    if (!ctx || !needed || ctx->module_count > WS_MAX_MODULES) return -1;
    const char *wanted = basename_of(needed);
    for (size_t i = 0; i < ctx->module_count; ++i)
        if (same(basename_of(ctx->modules[i].filename), wanted)) return (int)i;
    return -1;
}
static uint32_t elf_hash(const char *name) {
    uint32_t hash = 0;
    for (; *name; ++name) {
        hash = (hash << 4) + (unsigned char)*name;
        uint32_t top = hash & 0xf0000000u;
        if (top) hash ^= top >> 24;
        hash &= ~top;
    }
    return hash;
}
static int definition(const so_module *image, const char *name, uintptr_t *value) {
    /* Every library in the pinned APK has a SysV hash table. Bounded fallback
     * keeps synthetic test modules and non-hashed modules usable. */
    uint32_t index = 0, steps = 0;
    const uint32_t *chains = 0;
    if (image->hash && image->hash[0] && image->hash[1] <= image->num_dynsym) {
        index = image->hash[2 + elf_hash(name) % image->hash[0]];
        chains = image->hash + 2 + image->hash[0];
    }
    for (; steps < image->num_dynsym; ++steps) {
        if (chains && (!index || index >= image->hash[1])) break;
        if (index >= image->num_dynsym) break;
        const Elf32_Sym *symbol = &image->dynsym[index];
        if (symbol->st_shndx != SHN_UNDEF &&
            ELF32_ST_BIND(symbol->st_info) != STB_LOCAL &&
            same(image->dynstr + symbol->st_name, name)) {
            *value = symbol->st_value + (symbol->st_shndx == SHN_ABS ? 0 : image->load_addr);
            return 1;
        }
        index = chains ? chains[index] : index + 1;
    }
    return 0;
}
typedef struct { unsigned count, indices[WS_MAX_MODULES]; } dependencies;
static void enqueue_dependencies(const dependencies *graph, unsigned index,
                                 unsigned *queue, size_t *tail, uint32_t *seen) {
    for (unsigned i = 0; i < graph[index].count; ++i) {
        unsigned dependency = graph[index].indices[i];
        if (!(*seen & (1u << dependency))) {
            *seen |= 1u << dependency;
            queue[(*tail)++] = (unsigned)dependency;
        }
    }
}
static int resolve(const ws_link_context *ctx, const dependencies *graph, unsigned requester,
                   const Elf32_Sym *symbol, uintptr_t *value) {
    const so_module *image = ctx->modules[requester].image;
    if (symbol->st_shndx != SHN_UNDEF) {
        *value = symbol->st_value + (symbol->st_shndx == SHN_ABS ? 0 : image->load_addr);
        return 1;
    }
    const char *name = image->dynstr + symbol->st_name;
    for (size_t i = 0; i < ctx->platform_count; ++i) {
        if (same(ctx->platform[i].symbol, name)) {
            *value = ctx->platform[i].func;
            return *value != 0 || ELF32_ST_BIND(symbol->st_info) == STB_WEAK;
        }
    }
    unsigned queue[WS_MAX_MODULES];
    size_t head = 0, tail = 0;
    uint32_t seen = 1u << requester;
    enqueue_dependencies(graph, requester, queue, &tail, &seen);
    while (head < tail) {
        unsigned dependency = queue[head++];
        if (definition(ctx->modules[dependency].image, name, value)) return 1;
        enqueue_dependencies(graph, dependency, queue, &tail, &seen);
    }
    /* Java loads these modules one at a time in the manifest's order. A later
     * unrelated module must not retroactively satisfy an earlier weak import:
     * libv must not call libSystem's ObjC registration before it initializes. */
    for (size_t i = 0; i < requester; ++i)
        if (!(seen & (1u << i)) && definition(ctx->modules[i].image, name, value)) return 1;
    if (ELF32_ST_BIND(symbol->st_info) == STB_WEAK) { *value = 0; return 1; }
    return 0;
}
static int range_contains(uintptr_t base, size_t size, uintptr_t address) {
    return size >= sizeof(uint32_t) && address >= base && address - base <= size - sizeof(uint32_t);
}
static int mapped_word(const so_module *image, uintptr_t address) {
    if (range_contains(image->text_base, image->text_size, address)) return 1;
    for (int i = 0; i < image->n_data; ++i)
        if (range_contains(image->data_base[i], image->data_size[i], address)) return 1;
    return 0;
}
int ws_link_all(const ws_link_context *ctx) {
    if (!ctx || !ctx->modules || !ctx->module_count || ctx->module_count > WS_MAX_MODULES ||
        (ctx->platform_count && !ctx->platform)) return -1;
    for (size_t i = 0; i < ctx->module_count; ++i) {
        so_module *image = ctx->modules[i].image;
        if (!ctx->modules[i].filename || !image || !image->dynsym || !image->dynstr ||
            image->n_data < 0 || image->n_data > MAX_DATA_SEG ||
            (image->num_dynamic && !image->dynamic) ||
            (image->num_reldyn && !image->reldyn) || (image->num_relplt && !image->relplt))
            return -1;
        for (size_t j = 0; j < i; ++j)
            if (same(basename_of(ctx->modules[i].filename), basename_of(ctx->modules[j].filename))) return -1;
    }
    /* Resolve build-machine paths once. Repeating this for each symbol makes
     * Apportable's large Objective-C relocation set unnecessarily expensive. */
    dependencies graph[WS_MAX_MODULES];
    for (unsigned i = 0; i < ctx->module_count; ++i) {
        const so_module *image = ctx->modules[i].image;
        uint32_t seen = 0;
        graph[i].count = 0;
        for (unsigned j = 0; j < image->num_dynamic; ++j) {
            if (image->dynamic[j].d_tag != DT_NEEDED) continue;
            int dependency = ws_module_index(ctx, image->dynstr + image->dynamic[j].d_un.d_val);
            if (dependency >= 0 && !(seen & (1u << dependency))) {
                seen |= 1u << dependency;
                graph[i].indices[graph[i].count++] = (unsigned)dependency;
            }
        }
    }
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned failures = 0;
        for (unsigned module = 0; module < ctx->module_count; ++module) {
            so_module *image = ctx->modules[module].image;
            /* Many Objective-C references relocate the same imported class
             * repeatedly. Cache undefined-symbol resolution per image/pass;
             * this keeps 204k relocations from repeating whole symbol scans. */
            struct { uint32_t key; uintptr_t value; int found; } cache[1024];
            for (unsigned c = 0; c < 1024; ++c) cache[c].key = 0;
            for (uint64_t i = 0; i < (uint64_t)image->num_reldyn + image->num_relplt; ++i) {
                const Elf32_Rel *rel = i < image->num_reldyn ? &image->reldyn[i] : &image->relplt[i-image->num_reldyn];
                unsigned type = ELF32_R_TYPE(rel->r_info), symbol_index = ELF32_R_SYM(rel->r_info);
                if (type == R_ARM_NONE) continue;
                uintptr_t target = image->load_addr + rel->r_offset, value = 0;
                const char *name = "<invalid relocation>";
                int valid = symbol_index < image->num_dynsym && target >= image->load_addr && mapped_word(image, target);
                if (valid) {
                    name = image->dynstr + image->dynsym[symbol_index].st_name;
                    if (type == R_ARM_RELATIVE) { valid = !symbol_index; value = image->load_addr; }
                    else if (type == R_ARM_ABS32 || type == R_ARM_GLOB_DAT || type == R_ARM_JUMP_SLOT) {
                        if (image->dynsym[symbol_index].st_shndx != SHN_UNDEF) {
                            valid = resolve(ctx, graph, module, &image->dynsym[symbol_index], &value);
                        } else {
                            unsigned slot = symbol_index & 1023;
                            if (cache[slot].key != symbol_index + 1) {
                                cache[slot].value = 0;
                                cache[slot].found = resolve(ctx, graph, module, &image->dynsym[symbol_index], &cache[slot].value);
                                cache[slot].key = symbol_index + 1;
                            }
                            valid = cache[slot].found; value = cache[slot].value;
                        }
                    }
                    else valid = 0;
                }
                if (!valid) {
                    ++failures;
                    if (!pass && ctx->error) ctx->error(ctx->modules[module].filename, name, type);
                } else if (pass) {
                    /* R_ARM_ABS32 must retain its original addend for platform
                     * symbols as well as for symbols supplied by another ELF. */
                    int addend = type == R_ARM_ABS32 || type == R_ARM_RELATIVE;
                    if (!(target & 3)) {
                        uint32_t *word = (uint32_t *)target;
                        *word = (uint32_t)value + (addend ? *word : 0);
                    } else {
                        /* libv contains three legitimate packed, unaligned
                         * RELATIVE relocations. Avoid ARM alignment faults. */
                        volatile unsigned char *bytes = (volatile unsigned char *)target;
                        uint32_t previous = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
                                            (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
                        uint32_t result = (uint32_t)value + (addend ? previous : 0);
                        for (unsigned byte = 0; byte < 4; ++byte) bytes[byte] = result >> (byte * 8);
                    }
                }
            }
        }
        if (failures) return -1;
    }
    return 0;
}
