#include <iostream>

using namespace std;

int main(void)
{
	register double cnt = 0;
	while(cnt < 1e100)
	  cnt++;

	cout << "cnt = " << cnt << endl;

	return 0;
}
