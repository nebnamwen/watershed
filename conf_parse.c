else if (strcmp(key,"tgen_seed") == 0) { conf.tgen_seed = atol(val); }
else if (strcmp(key,"tgen_seed_oct") == 0) { conf.tgen_seed_oct = atoi(val); }
else if (strcmp(key,"tgen_hbase") == 0) { conf.tgen_hbase = atof(val); }
else if (strcmp(key,"tgen_hscale") == 0) { conf.tgen_hscale = atof(val); }
else if (strcmp(key,"tgen_skew") == 0) { conf.tgen_skew = atof(val); }
else if (strcmp(key,"flow_gravity") == 0) { conf.flow_gravity = atof(val); }
else if (strcmp(key,"flow_clamp") == 0) { conf.flow_clamp = atof(val); }
else if (strcmp(key,"flow_damp") == 0) { conf.flow_damp = atof(val); }
else if (strcmp(key,"flow_dp_ceil") == 0) { conf.flow_dp_ceil = atof(val); }
else if (strcmp(key,"ero_coeff") == 0) { conf.ero_coeff = atof(val); }
else if (strcmp(key,"ero_layer") == 0) { conf.ero_layer = atof(val); }
else if (strcmp(key,"ero_decay") == 0) { conf.ero_decay = atof(val); }
else if (strcmp(key,"vap_exc_htemp") == 0) { conf.vap_exc_htemp = atof(val); }
else if (strcmp(key,"vap_exc_pstd") == 0) { conf.vap_exc_pstd = atof(val); }
else if (strcmp(key,"vap_exc_evap") == 0) { conf.vap_exc_evap = atof(val); }
else if (strcmp(key,"vap_exc_cond") == 0) { conf.vap_exc_cond = atof(val); }
else if (strcmp(key,"vap_flow_diff") == 0) { conf.vap_flow_diff = atof(val); }
else if (strcmp(key,"vap_flow_wind") == 0) { conf.vap_flow_wind = atof(val); }
else if (strcmp(key,"vap_wind_x0") == 0) { conf.vap_wind_x0 = atof(val); }
else if (strcmp(key,"vap_wind_y0") == 0) { conf.vap_wind_y0 = atof(val); }
else if (strcmp(key,"vap_wind_x1") == 0) { conf.vap_wind_x1 = atof(val); }
else if (strcmp(key,"vap_wind_y1") == 0) { conf.vap_wind_y1 = atof(val); }
else if (strcmp(key,"vap_wind_circ") == 0) { conf.vap_wind_circ = atof(val); }
else if (strcmp(key,"vap_wind_period") == 0) { conf.vap_wind_period = atoi(val); }
else if (strcmp(key,"tide_amp") == 0) { conf.tide_amp = atof(val); }
else if (strcmp(key,"tide_period") == 0) { conf.tide_period = atoi(val); }
