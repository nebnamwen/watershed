print, next if /^#/ or /^$/;
($type, $name, $value) = split /\s+/;
print "$name=$value\n";
