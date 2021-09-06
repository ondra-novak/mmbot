#pragma once



namespace lexID {


template<typename T>
char *createLetters(T val, char *buff) {

	static char letters[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	if (val) {
		char *c = createLetters(val/36, buff);
		unsigned int ofs = (unsigned int)(val%36);
		*c = letters[ofs];
		return c+1;
	} else {
		return buff;
	}


}

template<typename T>
std::string create(T val) {
	char buff[64];

	char *c = createLetters(val, buff+1);
	unsigned int count = c-(buff+1);
	buff[0] = 'A'+count;
	*c = 0;
	return buff;
}


template<typename T>
T parse(std::string_view val, const T &initial) {
	T sum(initial);

	if (val.empty()) return sum;
	unsigned int count = val[0]-'A';
	if (count-1 > val.length()) return sum;

	for (unsigned int i = 0; i < count; i++) {
		char c = val[i+1];
		unsigned int v;
		if (isdigit(c)) {
			v = c - '0';
		} else if (isalpha(c)) {
			v = toupper(c) - 'A' + 10;
		} else {
			continue;
		}
		sum = sum * 36 + v;
	}
	return sum;
}


}
