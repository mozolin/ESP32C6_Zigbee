<?

$fn = "partitions.csv";
if(!empty($argv[1])) {
	$fn = $argv[1];
}
if(!file_exists($fn)) {
	echo "\nFile is not found!\n";
	exit;
}

$lens = [12,6,10,8,7,1];

$contents = file($fn);
foreach($contents as $line) {
	$line = trim($line);
	//if(substr($line, 0, 1) === "#") {
	//	continue;
	//}
	$arr = explode(",", $line);
	$num = count($arr);
	if($num !== 6) {
		continue;
	}
	$newLine = "";
	$idx = 0; 
	foreach($arr as $item) {
		$item = trim($item);
		if(preg_match("{0x\d*}si", $item)) {
			$s = (hexdec($item) / 1024)."K";
		} else {
			$s = $item;
		}
		if($idx < $num - 1) {
			$s .= ",";
		}
		$newLine .= str_pad($s, $lens[$idx], " ", STR_PAD_RIGHT);
		$idx++;
	}
	$newLine .= "\n";
	echo $newLine;
}
