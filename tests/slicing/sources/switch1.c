

int main(void)
{
	int a = 1;
	switch(a) {
		case 1:
			a = 4;
			break;
		case 2:
			a = 2;
			break;
		case 3:
			a = 3;
			break;
		case 4:
		case 5:
			a = 8;
			break;
		default:
			a = 3;
	}


	test_assert(a == 4);
	return 0;
}
