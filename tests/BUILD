conf=$1

clang++ $conf --std=c++17 -I../../libzt_playground/libzt/include/ -I../Reliable-UDP_ztsified/ -I./ -L../../libzt_playground/libzt/lib/release/linux-x86_64/ -lzt -lgtest -pthread test.cc byte_array.cc zt_service.cc zts_ip6_udp_socket.cc zts_ip6_rudp_socket.cc zts_exception.cc zts_event_connector.cc ../Reliable-UDP_ztsified/rudp.cc ../Reliable-UDP_ztsified/event.cc ../Reliable-UDP_ztsified/logging_lock.cc -o test
cd tester_d
clang++ $conf --std=c++17 -I../../../libzt_playground/libzt/include/ -I../../Reliable-UDP_ztsified/ -I../ -L../../../libzt_playground/libzt/lib/release/linux-x86_64/ -lzt -pthread tester.cc ../byte_array.cc ../zt_service.cc ../zts_ip6_udp_socket.cc ../zts_ip6_rudp_socket.cc ../zts_exception.cc ../zts_event_connector.cc ../../Reliable-UDP_ztsified/rudp.cc ../../Reliable-UDP_ztsified/event.cc ../../Reliable-UDP_ztsified/logging_lock.cc -o tester
cd ..
