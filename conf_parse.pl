next if /^#/ or /^$/;
($type, $name, $value) = split /\s+/;
$typec = { int => 'i', long => 'l', double => 'f' }->{$type}; 
print qq{else if (strcmp(key,"$name") == 0) { conf.$name = ato$typec(val); }\n};
