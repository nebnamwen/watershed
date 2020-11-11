next if /^#/ or /^$/;
($type, $name, $value) = split /\s+/;
print "$type $name;\n";
