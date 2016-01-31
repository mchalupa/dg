

int glob;
void setglob(void)
{
	glob = 8;
}

void (*funcarray[10])(void) = {setglob};

int main(void)
{
	funcarray[0]();
	test_assert(glob == 8);
	return 0;
}
