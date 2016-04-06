ip=127.0.0.1
port=31415

./client -h $ip -p $port -m 0 < samples/sample1.txt > out/0_sample1.out
./client -h $ip -p $port -m 0 < samples/sample2.txt > out/0_sample2.out
./client -h $ip -p $port -m 0 < samples/binary1.txt > out/0_binary1.out
./client -h $ip -p $port -m 0 < samples/binary2.jpg > out/0_binary2.out

./client -h $ip -p $port -m 1 < samples/sample1.txt > out/1_sample1.out
./client -h $ip -p $port -m 1 < samples/sample2.txt > out/1_sample2.out
./client -h $ip -p $port -m 1 < samples/binary1.txt > out/1_binary1.out
./client -h $ip -p $port -m 1 < samples/binary2.jpg > out/1_binary2.out

./client -h $ip -p $port -m 2 < samples/sample1.txt > out/2_sample1.out
./client -h $ip -p $port -m 2 < samples/sample2.txt > out/2_sample2.out
./client -h $ip -p $port -m 2 < samples/binary1.txt > out/2_binary1.out
./client -h $ip -p $port -m 2 < samples/binary2.jpg > out/2_binary2.out

