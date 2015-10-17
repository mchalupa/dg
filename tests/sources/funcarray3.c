#include <assert.h>

int glob;
void setglob(void)
{
	glob = 8;
}

void setglob2(void)
{
	glob = 13;
}

void (*funcarray[10])(void) = {setglob, setglob2};

void call(void (**funcarray)(void), int idx)
{
	funcarray[idx]();
}

int main(void)
{
	call(funcarray, 1);
	assert(glob == 13);
	return 0;
}
