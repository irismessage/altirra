#include <vd2/system/math.h>
#include <vd2/system/thread.h>
#include <stdio.h>

class Blah : public VDThread {
public:
	void ThreadRun() {
		for(int i=0; i<100; ++i)
			printf("%d\n", i);
	}
};

int main() {
	Blah b;

	b.ThreadStart();
	b.ThreadWait();
}
