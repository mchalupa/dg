

int glob;
void setglob(void)
{
	glob = 8;
}

void (*funcarray[10])(void) = {setglob};

void call(void (**funcarray)(void), int idx)
{
	funcarray[idx]();
}

int main(void)
{
	call(funcarray, 0);
	test_assert(glob == 8);
	return 0;
}
