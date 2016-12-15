--- src/cache_cf.c.orig	Tue Jul  1 20:42:41 2003
+++ src/cache_cf.c	Mon Jun  6 23:45:51 2005
@@ -828,6 +828,24 @@
 	delayFreeDelayPool(pool);
 	safe_free(cfg->rates[pool]);
     }
+    
+    cfg->rates[pool] = xmalloc(class * sizeof(delaySpec));
+    cfg->class[pool] = class;
+    cfg->rates[pool]->aggregate.restore_bps = cfg->rates[pool]->aggregate.max_bytes = -1;
+    cfg->rates[pool]->aggregate.restore_bps_orig = cfg->rates[pool]->aggregate.max_bytes_orig = -1;
+                                                                                                                             
+    if (cfg->class[pool] >= 3)
+        {
+        cfg->rates[pool]->network.restore_bps = cfg->rates[pool]->network.max_bytes = -1;
+                cfg->rates[pool]->network.restore_bps_orig = cfg->rates[pool]->network.max_bytes_orig = -1;
+                }
+                                                                                                                             
+    if (cfg->class[pool] >= 2)
+        {
+                cfg->rates[pool]->individual.restore_bps = cfg->rates[pool]->individual.max_bytes = -1;
+                cfg->rates[pool]->individual.restore_bps_orig = cfg->rates[pool]->individual.max_bytes_orig = -1;
+                }
+    
     /* Allocates a "delaySpecSet" just as large as needed for the class */
     cfg->rates[pool] = xmalloc(class * sizeof(delaySpec));
     cfg->class[pool] = class;
@@ -867,8 +885,10 @@
 	if (sscanf(token, "%d", &i) != 1)
 	    self_destruct();
 	ptr->restore_bps = i;
+	ptr->restore_bps_orig = i;
 	i = GetInteger();
 	ptr->max_bytes = i;
+	ptr->max_bytes_orig = i;
 	ptr++;
     }
     class = cfg->class[pool];
