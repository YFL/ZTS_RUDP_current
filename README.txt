To build the project:
cd to tests folder and run BUILD
To run the tests:
open two terminals and cd to the tests folder in one of them and to tester_d inside tests in the other one,
set the path to libzt in the LD_LIBRARY_PATH environment variable (export LD_LIBRARY_PATH=/path/to/libzt:$LD_LIBRARY_PATH),
then run the listening side first and the other side afterwards.
The test program is called test in tests folder and the other is called tester in the tester_d folder.

Check the test.cc and tester_d/tester.cc files. These are the files that contain the two sides of the communication.

In test.cc we can see actual gtest code, so this is the program that actually outputs something.
The tester.cc is just the means to have someone to communicate with.

Which program to run first is a question to be answered by the contents of the above mentioned files. 

The problem which has to be solved:

The locks are misbehaving in the Reliable-UDP lib. I had to modify it, to be able to use it with ZeroTier.
Plus I had to modify it to be able to use it in a multithreaded environment. The latter didn't came out quite good.

So basically the RUDP part is misbehaving in many ways. Sometimes the tests finish, but there are errors, sometimes
the whole thing seems to be just hanging. Wihle debugging I came to the knowledge, that the locks may cause this,
but I can't figure out exactly why aren't they working. The problematic mutexes are the ones for fd_event_handlers
and timeout_event_handlers. In event.c in the Reliable-UDP_ztsified directory you can check how they are used.

More debugging has to be done, but I simply can't get the time start working on it.
