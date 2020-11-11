print, next if /^#/;
($type, $name, $value) = split /\s+/;
print "$name=$value\n";
