

int main(void)
{
	int a = 1;

	/* OK, this is really ugly, but C supports it */
	switch(a) {
	if (a > 0) {
		case 1:
			a = 4;
			break;
		case 2:
			a = 2;
			break;
	} else {
		case -3:
			a = 3;
			break;
		case -4:
			a = 8;
			break;
	}
		default:
			a = 3;
	}


	test_assert(a == 4);
	return 0;
}
