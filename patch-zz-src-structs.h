--- src/structs.h.orig	Fri Sep 12 20:30:16 2003
+++ src/structs.h	Mon Jun  6 23:39:09 2005
@@ -348,6 +348,8 @@
 struct _delaySpec {
     int restore_bps;
     int max_bytes;
+    int restore_bps_orig;
+    int max_bytes_orig;
 };
 
 /* malloc()'d only as far as used (class * sizeof(delaySpec)!
