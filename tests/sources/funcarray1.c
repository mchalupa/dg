#include <assert.h>

int glob;
void setglob(void)
{
	glob = 8;
}

void (*funcarray[10])(void) = {setglob};

int main(void)
{
	funcarray[0]();
	assert(glob == 8);
	return 0;
}
