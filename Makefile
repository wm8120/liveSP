all:
	g++ -std=c++11 -I /home/wm/usr/boost_1_52_0/include/ -L /home/wm/usr/boost_1_52_0/lib/ -lboost_regex codegen.cpp -g -o codegen
systat: systat.cpp
	g++ -std=c++11 -I /home/wm/usr/boost_1_52_0/include/ -L /home/wm/usr/boost_1_52_0/lib/ systat.cpp -g -o systat
test: test_regex.cpp
	g++ -std=c++11 -I /home/wm/usr/boost_1_52_0/include/ -L /home/wm/usr/boost_1_52_0/lib/ -lboost_regex test_regex.cpp -g -o test_regex

