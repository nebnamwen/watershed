else if (strcmp(key,"tgen_seed") == 0) { conf.tgen_seed = atol(val); }
else if (strcmp(key,"tgen_seed_oct") == 0) { conf.tgen_seed_oct = atoi(val); }
else if (strcmp(key,"tgen_hbase") == 0) { conf.tgen_hbase = atof(val); }
else if (strcmp(key,"tgen_hscale") == 0) { conf.tgen_hscale = atof(val); }
else if (strcmp(key,"tgen_skew") == 0) { conf.tgen_skew = atof(val); }
else if (strcmp(key,"flow_gravity") == 0) { conf.flow_gravity = atof(val); }
else if (strcmp(key,"flow_clamp") == 0) { conf.flow_clamp = atof(val); }
else if (strcmp(key,"flow_damp") == 0) { conf.flow_damp = atof(val); }
