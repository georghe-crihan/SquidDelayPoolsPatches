--- src/delay_pools.c.orig	Thu Jun 19 02:53:35 2003
+++ src/delay_pools.c	Sat Jun 11 15:26:40 2005
@@ -69,11 +69,23 @@
     /* 256 entries */
     int network[256];
     /* 256 sets of (255 entries + 1 terminator byte) */
+    
+    //	we need to keep separate values for each network in order to 
+    //	manage the bandwidth control
+    int network_restore_bps[NET_MAP_SZ];
+    int network_max_bytes[NET_MAP_SZ];
+    
     unsigned char individual_map[NET_MAP_SZ][IND_MAP_SZ];
     /* Pack this into one bit per net */
     unsigned char individual_255_used[32];
     /* largest entry = (255<<8)+255 = 65535 */
     int individual[C3_IND_SZ];
+    
+    //	we need to keep separate values for each individual in order to 
+    //	manage the bandwidth control
+    int individual_restore_bps[C3_IND_SZ];
+    int individual_max_bytes[C3_IND_SZ];
+    
 };
 
 typedef struct _class1DelayPool class1DelayPool;
@@ -88,13 +100,26 @@
 
 typedef union _delayPool delayPool;
 
+#define	DOWN_GRADE	0.97
+#define	UP_GRADE	0.01
+
 static delayPool *delay_data = NULL;
 static fd_set delay_no_delay;
 static time_t delay_pools_last_update = 0;
 static hash_table *delay_id_ptr_hash = NULL;
 static long memory_used = 0;
+static long int excess_bytes = 0;
 
 static OBJH delayPoolStats;
+#define max_excess_round 10
+#define max_delay_pools 10
+static long int last_excess[max_delay_pools][max_excess_round];
+static int excess_round[max_delay_pools];
+static int common_individual_restore_bps[max_delay_pools];
+static void delayPoolsRestoreUpdate(void *unused);
+static void delayUpdateRestoreClass1(class1DelayPool * class1, delaySpecSet * rates, int incr, int class_num);
+static void delayUpdateRestoreClass3(class3DelayPool * class3, delaySpecSet * rates, int incr, int class_num);
+static time_t RestoreVal_last_update = 0;
 
 static unsigned int
 delayIdPtrHash(const void *key, unsigned int n)
@@ -141,11 +166,15 @@
 void
 delayInitDelayData(unsigned short pools)
 {
+    int i;
+    for (i = 0; i < max_delay_pools; i++) common_individual_restore_bps[i] = 0;
     if (!pools)
 	return;
     delay_data = xcalloc(pools, sizeof(*delay_data));
     memory_used += pools * sizeof(*delay_data);
     eventAdd("delayPoolsUpdate", delayPoolsUpdate, NULL, 1.0, 1);
+    eventAdd("delayPoolsRestoreUpdate", delayPoolsRestoreUpdate, NULL, 1.0, 1);
+    excess_bytes = 0;
     delay_id_ptr_hash = hash_create(delayIdPtrHashCmp, 256, delayIdPtrHash);
 }
 
@@ -235,6 +264,7 @@
     /* delaySetSpec may be pointer to partial structure so MUST pass by
      * reference.
      */
+    int i = 0;
     switch (class) {
     case 1:
 	delay_data[pool].class1->aggregate = (int) (((double) rates->aggregate.max_bytes *
@@ -253,6 +283,20 @@
 	delay_data[pool].class3->network_255_used = 0;
 	memset(&delay_data[pool].class3->individual_255_used, '\0',
 	    sizeof(delay_data[pool].class3->individual_255_used));
+	
+	/*	initialize the network and individual restore value variables */
+	for (i = 0; i < NET_MAP_SZ; i++)
+		{
+		delay_data[pool].class3->network_restore_bps[i]	= rates->network.restore_bps;
+		delay_data[pool].class3->network_max_bytes[i]	= rates->network.max_bytes;
+		}
+
+	for (i = 0; i < C3_IND_SZ; i++)
+		{
+		delay_data[pool].class3->individual_restore_bps[i]	= rates->individual.restore_bps;
+		delay_data[pool].class3->individual_max_bytes[i]	= rates->individual.max_bytes;
+		}
+	
 	break;
     default:
 	assert(0);
@@ -466,7 +510,7 @@
 }
 
 static void
-delayUpdateClass3(class3DelayPool * class3, delaySpecSet * rates, int incr)
+delayUpdateClass3(class3DelayPool * class3, delaySpecSet * rates, int incr, long int *excess_bytes, unsigned long * max_bandwidth, int pool)
 {
     int individual_restore_bytes, network_restore_bytes;
     int mpos;
@@ -474,10 +518,47 @@
     /* delaySetSpec may be pointer to partial structure so MUST pass by
      * reference.
      */
-    if (rates->aggregate.restore_bps != -1 &&
-	(class3->aggregate += rates->aggregate.restore_bps * incr) >
-	rates->aggregate.max_bytes)
-	class3->aggregate = rates->aggregate.max_bytes;
+    if (rates->aggregate.restore_bps != -1)
+       *max_bandwidth += rates->aggregate.restore_bps;
+    if (rates->aggregate.restore_bps != -1) {
+	if ((class3->aggregate += rates->aggregate.restore_bps * incr) >
+            rates->aggregate.max_bytes) {
+	   last_excess[pool][excess_round[pool]++] =
+           class3->aggregate - rates->aggregate.max_bytes;
+	   //debug(77, 1) ("SB test: delay_pools.c->delayUpdateClass3() pool[%d]->excess(%d) = %d\n",
+           //pool, *excess_bytes, (class3->aggregate - rates->aggregate.max_bytes));
+	   *excess_bytes += (class3->aggregate - rates->aggregate.max_bytes);
+	   class3->aggregate = rates->aggregate.max_bytes;
+	   debug(77, 2) ("DelayUpdateClass3: Excess_bytes=%d, C3 Aggregate=%d, Max_bytes=%d, incr=%d\n", 
+	    (int) *excess_bytes, class3->aggregate, rates->aggregate.max_bytes,
+            incr );
+	} else {
+	    if ((class3->aggregate < rates->aggregate.max_bytes) &&
+                (*excess_bytes >= 0)) {
+		if (*excess_bytes >
+                    (rates->aggregate.max_bytes - class3->aggregate)) {
+		   last_excess[pool][excess_round[pool]++] =
+                   class3->aggregate - rates->aggregate.max_bytes;
+		   //debug(77, 1) ("SB test: delay_pools.c->delayUpdateClass3() excess(%d)->pool[%d] = %d\n",
+                   //*excess_bytes, pool,
+                   //(rates->aggregate.max_bytes - class3->aggregate));
+		   *excess_bytes -= (rates->aggregate.max_bytes -
+                   class3->aggregate);
+		   class3->aggregate = rates->aggregate.max_bytes;
+	} else {
+		   last_excess[pool][excess_round[pool]++] = 0 - *excess_bytes;
+		   //debug(77, 1) ("SB test: delay_pools.c->delayUpdateClass3() excess(%d)->pool[%d] = %d\n",
+                   //*excess_bytes, pool, *excess_bytes);
+		   class3->aggregate += *excess_bytes;
+		   *excess_bytes = 0;
+	}
+}
+debug(77, 2) (
+"DelayUpdateClass3: Excess_bytes=%d, C3 Aggregate=%d, Max_bytes=%d\n",
+(int) *excess_bytes, class3->aggregate, rates->aggregate.max_bytes );
+}
+}
+	if (excess_round[pool] >= max_excess_round) excess_round[pool] = 0;
     /* the following line deliberately uses &, not &&, in an if statement
      * to avoid conditional execution
      */
@@ -506,7 +587,8 @@
 		    break;
 		assert(mpos < C3_IND_SZ);
 		if (class3->individual[mpos] != rates->individual.max_bytes &&
-		    (class3->individual[mpos] += individual_restore_bytes) >
+		//(class3->individual[mpos] += (class3->individual_restore_bps[mpos] * incr)) >
+		    (class3->individual[mpos] += (common_individual_restore_bps[pool] * incr)) >
 		    rates->individual.max_bytes)
 		    class3->individual[mpos] = rates->individual.max_bytes;
 		if (j == 254)
@@ -516,14 +598,15 @@
 		mpos |= 255;	/* this will set mpos to network 255 */
 		assert(mpos < C3_IND_SZ);
 		if (class3->individual[mpos] != rates->individual.max_bytes &&
-		    (class3->individual[mpos] += individual_restore_bytes) >
+		     //(class3->individual[mpos] += (class3->individual_restore_bps[mpos] * incr)) >
+		     (class3->individual[mpos] += (common_individual_restore_bps[pool] * incr)) >
 		    rates->individual.max_bytes)
 		    class3->individual[mpos] = rates->individual.max_bytes;
 	    }
 	}
 	if (network_restore_bytes != -incr &&
 	    class3->network[i] != rates->network.max_bytes &&
-	    (class3->network[i] += network_restore_bytes) >
+	    (class3->network[i] += (class3->network_restore_bps[i] * incr)) >
 	    rates->network.max_bytes)
 	    class3->network[i] = rates->network.max_bytes;
 	if (i == 254)
@@ -537,6 +620,8 @@
     int incr = squid_curtime - delay_pools_last_update;
     unsigned short i;
     unsigned char class;
+    unsigned long int max_bandwidth;
+
     if (!Config.Delay.pools)
 	return;
     eventAdd("delayPoolsUpdate", delayPoolsUpdate, NULL, 1.0, 1);
@@ -555,11 +640,14 @@
 	    delayUpdateClass2(delay_data[i].class2, Config.Delay.rates[i], incr);
 	    break;
 	case 3:
-	    delayUpdateClass3(delay_data[i].class3, Config.Delay.rates[i], incr);
+	    delayUpdateClass3(delay_data[i].class3, Config.Delay.rates[i], incr, &excess_bytes, &max_bandwidth, i);
+	    // delayUpdateClass3(delay_data[i].class3, Config.Delay.rates[i], incr);
 	    break;
 	default:
 	    assert(0);
 	}
+	if (excess_bytes > (max_bandwidth * 5))
+	    excess_bytes = (max_bandwidth * 5);
     }
 }
 
@@ -616,16 +704,26 @@
 void
 delayBytesIn(delay_id d, int qty)
 {
+    //debug(77, 1) ("SB test: delay_pools.c->delayBytesIn(%p, %d) start\n", &d, qty);
+    
     unsigned short position = d & 0xFFFF;
     unsigned short pool = (d >> 16) - 1;
     unsigned char class;
+    
+    int old_aggregate_val = 0;
+    int old_network_val = 0;
+    int old_individual_val = 0;
 
+    //debug(77, 1) ("SB test: delay_pools.c->delayBytesIn()->pool = %d\n", pool);
+    
     if (pool == 0xFFFF)
 	return;
     class = Config.Delay.class[pool];
     switch (class) {
     case 1:
 	delay_data[pool].class1->aggregate -= qty;
+		debug(77, 3) ("DelayBytesIn: Class1 Num=%d, Old Aggregate=%d, Qty In=%d New Aggregate=%d\n", 
+			(pool + 1), old_aggregate_val, qty, delay_data[pool].class1->aggregate);
 	return;
     case 2:
 	delay_data[pool].class2->aggregate -= qty;
@@ -635,6 +733,14 @@
 	delay_data[pool].class3->aggregate -= qty;
 	delay_data[pool].class3->network[position >> 8] -= qty;
 	delay_data[pool].class3->individual[position] -= qty;
+	debug(77, 3) ("DelayBytesIn: Class3 Num=%d, Network ID=%d, Individual ID=%d\n", (pool + 1), (position >> 8), position);
+	debug(77, 3) ("DelayBytesIn: \t\tOld Aggregate=%d,\tQty In=%d,\tNew Aggregate=%d\n",
+                        old_aggregate_val, qty, delay_data[pool].class3->aggregate );
+        debug(77, 3) ("DelayBytesIn: \t\tOld Network=%d,\tQty In=%d,\tNew Network=%d\n",
+                        old_network_val, qty, delay_data[pool].class3->network[position >> 8]);
+        debug(77, 3) ("DelayBytesIn: \t\tOld Individual=%d,\tQty In=%d\tNew Individual=%d\n",
+                        old_individual_val, qty,delay_data[pool].class3->individual[position]);
+
 	return;
     }
     fatalf("delayBytesWanted: Invalid class %d\n", class);
@@ -700,9 +806,9 @@
 	return;
     }
     storeAppendPrintf(sentry, "\tAggregate:\n");
-    storeAppendPrintf(sentry, "\t\tMax: %d\n", rate->aggregate.max_bytes);
-    storeAppendPrintf(sentry, "\t\tRestore: %d\n", rate->aggregate.restore_bps);
-    storeAppendPrintf(sentry, "\t\tCurrent: %d\n\n", ag);
+    storeAppendPrintf(sentry, "\t\tMax: %d, ", rate->aggregate.max_bytes);
+    storeAppendPrintf(sentry, "Restore: %d, ", rate->aggregate.restore_bps);
+    storeAppendPrintf(sentry, "Current: %d\n\n", ag);
 }
 
 static void
@@ -760,14 +866,20 @@
     unsigned int i;
     unsigned int j;
 
-    storeAppendPrintf(sentry, "Pool: %d\n\tClass: 3\n\n", pool + 1);
+    storeAppendPrintf(sentry, "Pool: %d\n\tClass: 3\n", pool + 1);
+    //storeAppendPrintf(sentry, "\tCommon Restore Value: %d\n\n", common_individual_restore_bps[pool]);
+    storeAppendPrintf(sentry, "\tLast %d seconds Excess (Not in order): \n\t\t", max_excess_round);
+    for (i = 0; i < max_excess_round; i++) {
+    	storeAppendPrintf(sentry, "%d, ", last_excess[pool][i]);
+    }
+    storeAppendPrintf(sentry, "\n\n");
     delayPoolStatsAg(sentry, rate, class3->aggregate);
     if (rate->network.restore_bps == -1) {
 	storeAppendPrintf(sentry, "\tNetwork:\n\t\tDisabled.");
     } else {
 	storeAppendPrintf(sentry, "\tNetwork:\n");
-	storeAppendPrintf(sentry, "\t\tMax: %d\n", rate->network.max_bytes);
-	storeAppendPrintf(sentry, "\t\tRate: %d\n", rate->network.restore_bps);
+	storeAppendPrintf(sentry, "\t\tMax: %d, ", rate->network.max_bytes);
+	storeAppendPrintf(sentry, "Rate: %d\n", rate->network.restore_bps);
 	storeAppendPrintf(sentry, "\t\tCurrent: ");
 	for (i = 0; i < NET_MAP_SZ; i++) {
 	    if (class3->network_map[i] == 255)
@@ -790,8 +902,9 @@
 	return;
     }
     storeAppendPrintf(sentry, "\tIndividual:\n");
-    storeAppendPrintf(sentry, "\t\tMax: %d\n", rate->individual.max_bytes);
-    storeAppendPrintf(sentry, "\t\tRate: %d\n", rate->individual.restore_bps);
+    storeAppendPrintf(sentry, "\t\tMax: %d, ", rate->individual.max_bytes);
+    //storeAppendPrintf(sentry, "\t\tRate: %d\n", rate->individual.restore_bps);
+    storeAppendPrintf(sentry, "Rate: %d\n", common_individual_restore_bps[pool]);
     for (i = 0; i < NET_MAP_SZ; i++) {
 	if (class3->network_map[i] == 255)
 	    break;
@@ -854,5 +967,287 @@
     }
     storeAppendPrintf(sentry, "Memory Used: %d bytes\n", (int) memory_used);
 }
+
+
+// main function for updating the restore values of the delay pool
+static void delayPoolsRestoreUpdate(void *unused)
+	{
+    unsigned short i;
+    unsigned char class;
+
+    int incr = squid_curtime - RestoreVal_last_update;
+    if (!Config.Delay.pools)
+		return;
+
+    eventAdd("delayPoolsRestoreUpdate", delayPoolsRestoreUpdate, NULL, 1.0, 1);
+    if (incr < 1)
+		return;
+    //for (i = 0; i < Config.Delay.pools; i++) {
+    //	
+    //}
+    for (i = 0; i < Config.Delay.pools; i++) 
+    	{
+		class = Config.Delay.class[i];
+		if (!class)
+			continue;
+		
+		switch (class) 
+			{
+			case 1:
+				delayUpdateRestoreClass1(delay_data[i].class1, Config.Delay.rates[i], incr, (i + 1));
+				break;
+/*			case 2:
+				delayUpdateClass2(delay_data[i].class2, Config.Delay.rates[i], incr);
+				break;*/
+			case 3:
+				delayUpdateRestoreClass3(delay_data[i].class3, Config.Delay.rates[i], incr, (i + 1));
+				break;
+/*			default:
+				assert(0);*/
+			}
+    	}
+	}
+
+
+static void delayUpdateRestoreClass1(class1DelayPool * class1, delaySpecSet * rates, int incr, int class_num)
+	{
+    /* delaySetSpec may be pointer to partial structure so MUST pass by
+     * reference.
+     */
+     
+	int temp_restore_val = 0;
+	int old_restore_val = 0;
+	
+    if (rates->aggregate.restore_bps != -1)
+    	{
+		old_restore_val = rates->aggregate.restore_bps;
+		if (class1->aggregate < 0)
+			{
+			rates->aggregate.restore_bps = rates->aggregate.restore_bps * DOWN_GRADE;
+			if (rates->aggregate.restore_bps < rates->aggregate.restore_bps_orig)
+				rates->aggregate.restore_bps = rates->aggregate.restore_bps_orig;
+			}
+		/*if (class1->aggregate == 0)
+			{
+			rates->aggregate.restore_bps = rates->aggregate.restore_bps * 0.75;
+			if (rates->aggregate.restore_bps < rates->aggregate.restore_bps_orig)
+				rates->aggregate.restore_bps = rates->aggregate.restore_bps_orig;
+			}*/
+		if ((class1->aggregate > 0) || (class1->aggregate == 0))
+			{
+			temp_restore_val = (int)(rates->aggregate.restore_bps * (1.0 + (class1->aggregate / rates->aggregate.max_bytes)));
+			rates->aggregate.restore_bps = (rates->aggregate.restore_bps * 0.9) + (temp_restore_val * 0.1);
+			if (rates->aggregate.restore_bps > rates->aggregate.max_bytes)
+				rates->aggregate.restore_bps = rates->aggregate.max_bytes;
+			}
+		}
+	
+	debug(77, 3) ("Restore Calc: Class1 Num=%d, Max Bytes=%d, Current Val=%d, Old Restore=%d, New Restore=%d Orig Restore=%d\n", 
+		class_num, rates->aggregate.max_bytes, class1->aggregate, old_restore_val, rates->aggregate.restore_bps, rates->aggregate.restore_bps_orig);
+	}
+
+
+//	calculate the new restore values for a Class 3 delay pool and update the current values
+static void delayUpdateRestoreClass3(class3DelayPool * class3, delaySpecSet * rates, int incr, int class_num)
+	{
+    unsigned int i, j;
+    float 	temp_aggregate;
+    float	temp_max_bytes;
+    float 	temp_restore;
+    
+	int old_restore_val = 0;
+	int toggle_max		= 0;
+	int mpos			= 0;
+    
+	debug(77, 3) ("Restore Calc: Class3 Num=%d\n", class_num );
+	debug(77, 3) ("\t\tAggregate: Max_Bytes=%d, Current_Agg=%d, Restore_Val=%d\n", 
+		rates->aggregate.max_bytes, class3->aggregate, rates->aggregate.restore_bps);
+	toggle_max = (int)(rates->aggregate.max_bytes / 10);
+		   
+	
+	// update the network value
+	/* the following line deliberately uses &, not &&, in an if statement
+     * to avoid conditional execution
+     */
+
+    if (rates->network.restore_bps == -1)
+		{
+		debug(77, 3) ("\t\tNetwork: Disabled\n");
+		}
+	else
+		{
+		debug(77, 3) ("\t\tNetwork: Values are ignored in dynamic calculations\n");
+		}
+	
+	// if individual values are disabled exit function from here
+    if (rates->individual.restore_bps == -1)
+    	{
+		debug(77, 3) ("\t\tIndividual Disabled.");
+		return;
+		}
+    
+    if (common_individual_restore_bps[class_num - 1] <= 0) common_individual_restore_bps[class_num - 1] =  rates->individual.restore_bps;
+    if (class3->aggregate < toggle_max ) {
+    	common_individual_restore_bps[class_num - 1] = common_individual_restore_bps[class_num - 1] * DOWN_GRADE;
+    } else {
+    	//common_individual_restore_bps[class_num - 1] = common_individual_restore_bps[class_num - 1] * UP_GRADE;
+    	
+	temp_restore	= common_individual_restore_bps[class_num - 1];
+	temp_max_bytes	= rates->aggregate.max_bytes;
+	temp_aggregate	= class3->aggregate;
+
+	temp_restore = temp_restore * (1.0 + (UP_GRADE * (temp_aggregate / temp_max_bytes)));
+	if ((temp_restore - common_individual_restore_bps[class_num - 1]) <= 1.0) {
+		common_individual_restore_bps[class_num - 1]++;
+	} else {
+		common_individual_restore_bps[class_num - 1] = temp_restore;
+	}
+	
+	if (common_individual_restore_bps[class_num - 1] > rates->aggregate.restore_bps)
+		common_individual_restore_bps[class_num - 1] = rates->aggregate.restore_bps; 
+    }
+    debug(77, 1) ("SB Test: delay_pools.c->delayUpdateRestoreClass3()->common_individual_restore_bps[%d] = %d\n", class_num - 1, common_individual_restore_bps[class_num - 1]);
+    
+    for (i = 0; i < NET_MAP_SZ; i++)
+    	{
+		if (class3->network_map[i] == 255)
+			break;
+
+		for (j = 0; j < IND_MAP_SZ; j++)
+			{
+			if (class3->individual_map[i][j] == 255)
+				break;
+			
+			mpos = (i << 8) | j;
+			// calculate the new individual restore values
+			old_restore_val = class3->individual_restore_bps[mpos];
+			if (class3->aggregate < toggle_max )
+				{
+				class3->individual_restore_bps[mpos] = class3->individual_restore_bps[mpos] * DOWN_GRADE;
+				if (class3->individual_restore_bps[mpos] < rates->individual.restore_bps)
+					class3->individual_restore_bps[mpos] = rates->individual.restore_bps;
+				}
+			if (class3->aggregate >= toggle_max) 
+				{
+				temp_restore	= class3->individual_restore_bps[mpos];
+				temp_max_bytes	= rates->aggregate.max_bytes;
+				temp_aggregate	= class3->aggregate;
+
+				temp_restore = temp_restore * (1.0 + (UP_GRADE * (temp_aggregate / temp_max_bytes)));
+				class3->individual_restore_bps[mpos]	= temp_restore;
+				if (class3->individual_restore_bps[mpos] > rates->aggregate.restore_bps)
+					//class3->individual_restore_bps[mpos] = rates->aggregate.restore_bps;
+					//class3->individual_restore_bps[mpos] = rates->individual.max_bytes;
+					class3->individual_restore_bps[mpos] = rates->aggregate.max_bytes;
+				}
+		
+			debug(77, 3) ("\t\tIndividual ID=%d, Max Bytes=%d, Current Agg=%d, Old Restore=%d, New Restore=%d Orig Restore=%d\n", 
+			class3->individual_map[i][j], class3->individual_max_bytes[mpos], class3->individual[mpos], old_restore_val, class3->individual_restore_bps[mpos], rates->individual.restore_bps_orig);
+
+			}
+		
+		if (class3->individual_255_used[i / 8] & (1 << (i % 8)))
+			{
+
+			// calculate the new individual restore values
+			old_restore_val = class3->individual_restore_bps[mpos];
+			if (class3->aggregate < toggle_max )
+				{
+				class3->individual_restore_bps[mpos] = class3->individual_restore_bps[mpos] * DOWN_GRADE;
+				if (class3->individual_restore_bps[mpos] < rates->individual.restore_bps)
+					class3->individual_restore_bps[mpos] = rates->individual.restore_bps;
+				}
+
+			if (class3->aggregate >= toggle_max) 
+				{
+				temp_restore	= class3->individual_restore_bps[mpos];
+				temp_max_bytes	= rates->aggregate.max_bytes;
+				temp_aggregate	= class3->aggregate;
+
+				temp_restore = temp_restore * (1.0 + (UP_GRADE * (temp_aggregate / temp_max_bytes)));
+				class3->individual_restore_bps[mpos]	= temp_restore;
+				if (class3->individual_restore_bps[mpos] > rates->aggregate.restore_bps)
+					//class3->individual_restore_bps[mpos] = rates->aggregate.restore_bps;
+					class3->individual_restore_bps[mpos] = rates->individual.max_bytes;
+					class3->individual_restore_bps[mpos] = rates->aggregate.max_bytes;
+				}
+		
+			debug(77, 3) ("\t\tIndividual ID=%d, Max Bytes=%d, Current Agg=%d, Old Restore=%d, New Restore=%d Orig Restore=%d\n", 
+			class3->individual_map[i][j], class3->individual_max_bytes[mpos], class3->individual[mpos], old_restore_val, class3->individual_restore_bps[mpos], rates->individual.restore_bps_orig);
+
+			}
+		}
+	
+	debug(77, 2) ("\t\tPool aggregate=%d, individual_restore_val=%d\n", class3->aggregate, class3->individual_restore_bps[1]);
+		
+    if (class3->network_255_used)
+    	{
+		for (j = 0; j < IND_MAP_SZ; j++)
+			{
+			if (class3->individual_map[255][j] == 255)
+				break;
+			
+			// calculate the new individual restore values
+			old_restore_val = class3->individual_restore_bps[mpos];
+			if (class3->aggregate < toggle_max )
+				{
+				class3->individual_restore_bps[mpos] = class3->individual_restore_bps[mpos] * DOWN_GRADE;
+				if (class3->individual_restore_bps[mpos] < rates->individual.restore_bps)
+					class3->individual_restore_bps[mpos] = rates->individual.restore_bps;
+				}
+
+			if (class3->aggregate >= toggle_max) 
+				{
+				temp_restore	= class3->individual_restore_bps[mpos];
+				temp_max_bytes	= rates->aggregate.max_bytes;
+				temp_aggregate	= class3->aggregate;
+
+				temp_restore = temp_restore * (1.0 + (UP_GRADE * (temp_aggregate / temp_max_bytes)));
+				class3->individual_restore_bps[mpos]	= temp_restore;
+				if (class3->individual_restore_bps[mpos] > rates->aggregate.restore_bps)
+					//class3->individual_restore_bps[mpos] = rates->aggregate.restore_bps;
+					//class3->individual_restore_bps[mpos] = rates->individual.max_bytes;
+					class3->individual_restore_bps[mpos] = rates->aggregate.max_bytes;
+				}
+		
+			debug(77, 3) ("\t\tIndividual ID=%d, Max Bytes=%d, Current Agg=%d, Old Restore=%d, New Restore=%d Orig Restore=%d\n", 
+			class3->individual_map[i][j], class3->individual_max_bytes[mpos], class3->individual[mpos], old_restore_val, class3->individual_restore_bps[mpos], rates->individual.restore_bps_orig);
+
+			}
+		
+		if (class3->individual_255_used[255 / 8] & (1 << (255 % 8)))
+			{
+			
+			// calculate the new individual restore values
+			old_restore_val = class3->individual_restore_bps[mpos];
+			if (class3->aggregate < toggle_max )
+				{
+				class3->individual_restore_bps[mpos] = class3->individual_restore_bps[mpos] * DOWN_GRADE;
+				if (class3->individual_restore_bps[mpos] < rates->individual.restore_bps)
+					class3->individual_restore_bps[mpos] = rates->individual.restore_bps;
+				}
+
+			if (class3->aggregate >= toggle_max) 
+				{
+				temp_restore	= class3->individual_restore_bps[mpos];
+				temp_max_bytes	= rates->aggregate.max_bytes;
+				temp_aggregate	= class3->aggregate;
+
+				temp_restore = temp_restore * (1.0 + (UP_GRADE * (temp_aggregate / temp_max_bytes)));
+				class3->individual_restore_bps[mpos]	= temp_restore;
+				if (class3->individual_restore_bps[mpos] > rates->aggregate.restore_bps)
+					//class3->individual_restore_bps[mpos] = rates->aggregate.restore_bps;
+					//class3->individual_restore_bps[mpos] = rates->individual.max_bytes;
+					class3->individual_restore_bps[mpos] = rates->aggregate.max_bytes;
+				}
+		
+			debug(77, 3) ("\t\tIndividual ID=%d, Max Bytes=%d, Current Agg=%d, Old Restore=%d, New Restore=%d Orig Restore=%d\n", 
+			class3->individual_map[i][j], class3->individual_max_bytes[mpos], class3->individual[mpos], old_restore_val, class3->individual_restore_bps[mpos], rates->individual.restore_bps_orig);
+
+			
+			}
+		}
+	}	
+
 
 #endif
