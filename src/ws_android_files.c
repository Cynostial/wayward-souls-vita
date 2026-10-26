#include "ws_android.h"
#include "ws_paths.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static void fail(JNIEnv *e,const char *type,const char *message){
    jclass c=(*e)->FindClass(e,type);if(c){(*e)->ThrowNew(e,c,message);(*e)->DeleteLocalRef(e,c);}
}
static jfieldID path_field(JNIEnv *e){
    jclass c=(*e)->FindClass(e,"java/io/File");
    jfieldID f=c?(*e)->GetFieldID(e,c,"path","Ljava/lang/String;"):NULL;
    (*e)->DeleteLocalRef(e,c);return f;
}
static jstring path_string(JNIEnv *e,jobject file){
    jfieldID f=path_field(e);return f?(*e)->GetObjectField(e,file,f):NULL;
}
static int copy_path(JNIEnv *e,jstring value,char out[WS_PATH_MAX]){
    if(!value){fail(e,"java/lang/NullPointerException","File path is null");return -1;}
    const char *s=(*e)->GetStringUTFChars(e,value,NULL);if(!s)return -1;
    size_t size=strlen(s);if(size<WS_PATH_MAX)memcpy(out,s,size+1);
    (*e)->ReleaseStringUTFChars(e,value,s);
    if(size>=WS_PATH_MAX){fail(e,"java/lang/IllegalArgumentException","File path is too long");return -1;}
    return 0;
}
static int get_path(JNIEnv *e,jobject file,char out[WS_PATH_MAX]){
    jstring value=path_string(e,file);int result=copy_path(e,value,out);(*e)->DeleteLocalRef(e,value);return result;
}
static int absolute_path(JNIEnv *e,jobject file,char out[WS_PATH_MAX]){
    char path[WS_PATH_MAX];if(get_path(e,file,path))return -1;
    int n=snprintf(out,WS_PATH_MAX,"%s%s",path[0]=='/'?"":"/wayward/",path);
    if(n<0||n>=WS_PATH_MAX){fail(e,"java/lang/IllegalArgumentException","File path is too long");return -1;}
    return 0;
}
static int native_path(JNIEnv *e,jobject file,char out[WS_PATH_MAX]){
    char guest[WS_PATH_MAX];return absolute_path(e,file,guest)||ws_path_resolve(guest,out,WS_PATH_MAX)?-1:0;
}
jobject ws_android_file(JNIEnv *e,const char *path){
    jobject file=ws_jni_vm_new_instance(e,"java/io/File",NULL);if(!file)return NULL;
    jfieldID field=path_field(e);jstring text=field?(*e)->NewStringUTF(e,path):NULL;
    if(text)(*e)->SetObjectField(e,file,field,text);
    (*e)->DeleteLocalRef(e,text);
    if(!text||(*e)->ExceptionCheck(e)){(*e)->DeleteLocalRef(e,file);return NULL;}
    return file;
}
static jvalue initialize(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    int kind=(int)(intptr_t)ctx;(void)n;char path[WS_PATH_MAX],parent[WS_PATH_MAX]={0},child[WS_PATH_MAX];
    jstring parent_text=NULL;
    if(!kind){if(copy_path(e,a[0].l,path))return (jvalue){0};}
    else{
        if(copy_path(e,a[1].l,child))return (jvalue){0};
        parent_text=kind==1&&a[0].l?path_string(e,a[0].l):a[0].l;
        if(parent_text&&copy_path(e,parent_text,parent))goto end;
        int length=snprintf(path,sizeof(path),"%s%s%s",parent,*parent&&*child?"/":"",child[0]=='/'&&*parent?child+1:child);
        if(length<0||length>=(int)sizeof(path)){fail(e,"java/lang/IllegalArgumentException","File path is too long");goto end;}
    }
    /* java.io.File normalizes repeated separators and trailing separators. */
    size_t at=0;for(size_t i=0;path[i];++i)if(path[i]!='/'||!at||path[at-1]!='/')path[at++]=path[i];
    while(at>1&&path[at-1]=='/')--at;
    path[at]=0;
    jfieldID field=path_field(e);jstring text=(*e)->NewStringUTF(e,path);
    if(field&&text)(*e)->SetObjectField(e,self,field,text);
    (*e)->DeleteLocalRef(e,text);
end:
    if(kind==1)(*e)->DeleteLocalRef(e,parent_text);
    return (jvalue){0};
}
static jvalue path_method(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;int kind=(int)(intptr_t)ctx;char path[WS_PATH_MAX];jvalue out={0};
    if(kind==0){out.l=path_string(e,self);return out;}
    if((kind<=2?absolute_path(e,self,path):get_path(e,self,path)))return out;
    if(kind==2){out.l=ws_android_file(e,path);return out;}
    char *text=path,*slash=strrchr(path,'/');
    if(kind==3)text=slash?slash+1:path;
    if(kind==4||kind==5){if(!slash)return out;if(slash==path)slash[1]=0;else *slash=0;}
    out.l=kind==5?ws_android_file(e,text):(*e)->NewStringUTF(e,text);return out;
}
static jvalue inspect(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;char path[WS_PATH_MAX];jvalue out={0};int kind=(int)(intptr_t)ctx;
    if(kind==4){if(!get_path(e,self,path))out.z=path[0]=='/';return out;}
    if(native_path(e,self,path))return out;
    struct stat s;
    if(!stat(path,&s))switch(kind){
    case 0:out.z=1;break;
    case 1:out.z=S_ISREG(s.st_mode);break;
    case 2:out.z=S_ISDIR(s.st_mode);break;
    case 3:out.j=(jlong)s.st_size;break;
    case 5:out.j=(jlong)s.st_mtime*1000;break;
    }
    return out;
}
static int make_directory(const char *path){
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path,0700);
#endif
}
static jvalue make_dirs(void *ctx,JNIEnv *e,jobject self,const jvalue *a,size_t n){
    (void)a;(void)n;char path[WS_PATH_MAX];jvalue out={0};
    if(native_path(e,self,path))return out;
    if(!make_directory(path)){out.z=1;return out;}
    if(!ctx||errno!=ENOENT)return out;
    /* The complete resolved target was checked by ws_path_resolve before any
     * creation. Existing prefixes (including ux0: and host drives) stay put. */
    for(char *p=path+1;*p;++p)if(*p=='/'||*p=='\\'){
        char separator=*p;*p=0;int result=make_directory(path);*p=separator;
        if(result&&errno!=EEXIST)return out;
    }
    out.z=!make_directory(path);return out;
}
int ws_android_files_init(ws_java *j){
    if(ws_jni_vm_define_field(j->vm,"java/io/File","path","Ljava/lang/String;",0))return -1;
#define M(name,sig,fn,arg) if(ws_jni_vm_define_method(j->vm,"java/io/File",name,sig,0,fn,(void *)(intptr_t)arg))return -1
    M("<init>","(Ljava/lang/String;)V",initialize,0);
    M("<init>","(Ljava/io/File;Ljava/lang/String;)V",initialize,1);
    M("<init>","(Ljava/lang/String;Ljava/lang/String;)V",initialize,2);
    M("getPath","()Ljava/lang/String;",path_method,0);M("toString","()Ljava/lang/String;",path_method,0);
    M("getAbsolutePath","()Ljava/lang/String;",path_method,1);M("getAbsoluteFile","()Ljava/io/File;",path_method,2);
    M("getName","()Ljava/lang/String;",path_method,3);M("getParent","()Ljava/lang/String;",path_method,4);M("getParentFile","()Ljava/io/File;",path_method,5);
    M("exists","()Z",inspect,0);M("isFile","()Z",inspect,1);M("isDirectory","()Z",inspect,2);
    M("length","()J",inspect,3);M("isAbsolute","()Z",inspect,4);M("lastModified","()J",inspect,5);
    M("mkdir","()Z",make_dirs,0);M("mkdirs","()Z",make_dirs,1);
#undef M
    return 0;
}
