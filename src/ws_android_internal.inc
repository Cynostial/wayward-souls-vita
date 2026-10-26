/* Private URI inheritance absent from android.jar's public API.
 * AOSP frameworks/base android-4.2.2_r1 core/java/android/net/Uri.java,
 * declarations at lines 1029 and 1130. Public method signatures inherit Uri.
 * This declares types only, not implementations or private constructors. */
WS_CLASS("android/net/Uri$AbstractHierarchicalUri","android/net/Uri")
WS_CLASS("android/net/Uri$HierarchicalUri","android/net/Uri$AbstractHierarchicalUri")
/* Queried by the original libverde constructor through BridgeKit. Declaring
 * the field does not supply an Android SkBitmap pointer. Bitmap operations
 * must use the bounded adapter, never dereference its default zero value. */
WS_FIELD("android/graphics/Bitmap","mNativeBitmap","I",0)
/* android-4.2.2_r1/graphics/java/android/graphics/Bitmap.java */
WS_FIELD("android/graphics/Bitmap","mWidth","I",0)
WS_FIELD("android/graphics/Bitmap","mHeight","I",0)
WS_FIELD("android/graphics/Bitmap","mDensity","I",0)
WS_FIELD("android/graphics/Bitmap","mIsMutable","Z",0)
WS_FIELD("android/graphics/Bitmap","mRecycled","Z",0)
WS_FIELD("android/graphics/Bitmap","mNinePatchChunk","[B",0)
WS_FIELD("android/graphics/Bitmap","mLayoutBounds","[I",0)
/* Android View's hidden protected layout method is inherited by the APK View. */
WS_METHOD("android/view/View","setFrame","(IIII)Z",0)
