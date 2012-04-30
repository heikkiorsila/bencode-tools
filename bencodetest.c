#include <bencodetools/bencode.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void test_encode(const struct bencode *b, const char *s, size_t len)
{
	size_t encoded_len = 0;
	char *data = ben_encode(&encoded_len, b);

	assert(encoded_len <= len);
	assert(memcmp(data, s, encoded_len) == 0);

	free(data);
}

static void booltest(const char *s, size_t len, int expected, int experror)
{
	struct bencode_bool *b;
	size_t off = 0;
	int error;
	b = (struct bencode_bool *) ben_decode2(s, len, &off, &error);
	if (experror != error) {
		fprintf(stderr, "%s/%zd should got code %d but got %d\n", s, len, experror, error);
		exit(1);
	}
	if (experror == BEN_OK && b == NULL) {
		fprintf(stderr, "%s/%zd should have succeeded\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK && b->b != expected) {
		fprintf(stderr, "%s/%zd should have value %d\n", s, len, expected);
		exit(1);
	}
	if (experror != BEN_OK && b != NULL) {
		fprintf(stderr, "%s/%zd should have failed\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK)
		test_encode((struct bencode *) b, s, len);
	ben_free((struct bencode *) b);
}

static void inttest(const char *s, size_t len, long long expected, int experror)
{
	struct bencode_int *b;
	size_t off = 0;
	int error;
	b = (struct bencode_int *) ben_decode2(s, len, &off, &error);
	if (error != experror) {
		fprintf(stderr, "%s/%zd should get code %d but got %d\n", s, len, experror, error);
		exit(1);
	}
	if (experror == BEN_OK && b == NULL) {
		fprintf(stderr, "%s/%zd should have succeeded\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK && b->ll != expected) {
		fprintf(stderr, "%s/%zd should have value %lld\n", s, len, expected);
		exit(1);
	}
	if (experror != BEN_OK && b != NULL) {
		fprintf(stderr, "%s/%zd should have failed\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK)
		test_encode((struct bencode *) b, s, len);
	ben_free((struct bencode *) b);
}

static void strtest(const char *s, size_t len, const char *expected, int experror)
{
	struct bencode_str *b;
	size_t off = 0;
	int error;
	b = (struct bencode_str *) ben_decode2(s, len, &off, &error);
	if (error != experror) {
		fprintf(stderr, "%s/%zd should get code %d but got %d\n", s, len, experror, error);
		exit(1);
	}
	if (experror == BEN_OK && b == NULL) {
		fprintf(stderr, "%s/%zd should have succeeded\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK && memcmp(b->s, expected, strlen(expected)) != 0) {
		fprintf(stderr, "%s/%zd should have value %s\n", s, len, expected);
		exit(1);
	}
	if (experror != BEN_OK && b != NULL) {
		fprintf(stderr, "%s/%zd should have failed\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK)
		test_encode((struct bencode *) b, s, len);
	ben_free((struct bencode *) b);
}

static void listtest(const char *s, size_t len, size_t expected, int experror)
{
	struct bencode_list *b;
	size_t off = 0;
	int error;
	b = (struct bencode_list *) ben_decode2(s, len, &off, &error);
	if (error != experror) {
		fprintf(stderr, "%s/%zd should get code %d but got %d\n", s, len, experror, error);
		exit(1);
	}
	if (experror == BEN_OK && b == NULL) {
		fprintf(stderr, "%s/%zd should have succeeded\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK && b->n != expected) {
		fprintf(stderr, "%s/%zd should have %zu entries\n", s, len, expected);
		exit(1);
	}
	if (experror != BEN_OK && b != NULL) {
		fprintf(stderr, "%s/%zd should have failed\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK)
		test_encode((struct bencode *) b, s, len);
	ben_free((struct bencode *) b);
}

static void dicttest(const char *s, size_t len, size_t expected, int experror)
{
	struct bencode_dict *b;
	size_t off = 0;
	int error;
	b = (struct bencode_dict *) ben_decode2(s, len, &off, &error);
	if (error != experror) {
		fprintf(stderr, "%s/%zd should get code %d but got %d\n", s, len, experror, error);
		exit(1);
	}
	if (experror == BEN_OK && b == NULL) {
		fprintf(stderr, "%s/%zd should have succeeded\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK && b->n != expected) {
		fprintf(stderr, "%s/%zd should have %zu entries\n", s, len, expected);
		exit(1);
	}
	if (experror != BEN_OK && b != NULL) {
		fprintf(stderr, "%s/%zd should have failed\n", s, len);
		exit(1);
	}
	if (experror == BEN_OK)
		test_encode((struct bencode *) b, s, len);
	ben_free((struct bencode *) b);
}

static void print_tests(void)
{	
	struct test {
		char *encoded;
		size_t len;
		char *expected;
	};
	const struct test table[] = {
		{"d3:foo3:bare", -1, "{'foo': 'bar'}"},
		{"di0e0:i1e0:e", -1, "{0: '', 1: ''}"},
		{"di0eli1eei1e0:e", -1, "{0: [1], 1: ''}"},
		{"di0eli1ei2eei1e0:e", -1, "{0: [1, 2], 1: ''}"},
		{"1:\x00", 3, "'\\x00'"},
		{"1:\x01", 3, "'\\x01'"},
		{"1:\a", 3, "'\\x07'"},
		{"1:\b", 3, "'\\x08'"},
		{"1:\t", 3, "'\\x09'"},
		{"1:\n", 3, "'\\x0a'"},
		{"1:\r", 3, "'\\x0d'"},
		{"1: ", 3, "' '"},
		{"1:0", 3, "'0'"},
		{"1:A", 3, "'A'"},
		{"1:a", 3, "'a'"},
		{"1:\\", 3, "'\\\\'"},
		{"1:'", 3, "'\\''"},
		{"1:\x7f", 3, "'\\x7f'"},
		{"1:\xff", 3, "'\\xff'"},
		{"b0", -1, "False"},
		{"b1", -1, "True"},
		{"i0e", -1, "0"},
		{"i-1e", -1, "-1"},
		{"0:", -1, "''"},
		{"3:foo", -1, "'foo'"},
		{"le", -1, "[]"},
		{"li0ee", -1, "[0]"},
		{"li0ei1ee", -1, "[0, 1]"},
		{NULL, 0, NULL}};

	size_t i;
	char *printed;
	struct bencode *b;

	for (i = 0; table[i].encoded != NULL; i++) {
		const char *encoded = table[i].encoded;
		size_t len = table[i].len;
		const char *expected = table[i].expected;
		if (len == -1)
			len = strlen(encoded);
		b = ben_decode(encoded, len);
		if (b == NULL) {
			fprintf(stderr, "Failed to decode: %s\n", encoded);
			exit(1);
		}
		printed = ben_print(b);
		if (printed == NULL) {
			fprintf(stderr, "Failed to print: %s\n", expected);
			exit(1);
		}
		if (strcmp(printed, expected) != 0) {
			fprintf(stderr, "Invalid print output: %s vs %s\n", printed, expected);
			exit(1);
		}
		ben_free(b);
		free(printed);
	}
}

static size_t test_encoded_size(struct bencode *b)
{
	size_t s = ben_encoded_size(b);
	ben_free(b);
	return s;
}

static void encoded_size_tests(void)
{
	struct bencode *key;
	struct bencode *value;
	struct bencode *b;
	struct bencode *d;
	struct bencode *l;
	char data[4096];
	size_t s;
	size_t pos;
	size_t n;

	assert(test_encoded_size(ben_bool(0)) == 2);
	assert(test_encoded_size(ben_bool(1)) == 2);

	assert(test_encoded_size(ben_int(-10)) == 5);
	assert(test_encoded_size(ben_int(-1)) == 4);
	assert(test_encoded_size(ben_int(0)) == 3);
	assert(test_encoded_size(ben_int(1)) == 3);
	assert(test_encoded_size(ben_int(10)) == 4);

	assert(test_encoded_size(ben_str("marklar")) == 9);

	l = ben_list();
	assert(ben_encoded_size(l) == 2);
	ben_list_append(l, ben_int(0));
	assert(ben_encoded_size(l) == 5);
	ben_list_append(l, ben_str("marklar"));
	assert(ben_encoded_size(l) == 14);
	s = 0;
	n = 0;
	ben_list_for_each(b, pos, l) {
		s += ben_encoded_size(b);
		n++;
	}
	assert(s == 12);
	assert(n == 2);
	ben_free(l);
	l = NULL;

	d = ben_dict();
	assert(ben_encoded_size(d) == 2);
	ben_dict_set(d, ben_int(1), ben_str(""));
	ben_dict_set(d, ben_int(0), ben_str(""));
	ben_dict_set(d, ben_int(0), ben_str(""));
	assert(ben_dict_len(d) == 2);
	s = ben_encode2(data, sizeof data, d);
	assert(s == 12);
	assert(memcmp(data, "di0e0:i1e0:e", s) == 0);
	s = 0;
	n = 0;
	ben_dict_for_each(key, value, pos, d) {
		s += ben_encoded_size(key);
		s += ben_encoded_size(value);
		n++;
	}
	assert(s == 10);
	assert(n == 2);
	ben_free(d);
}

static void testvectors(const char **vec, int success)
{
	while (*vec != NULL) {
		struct bencode * b = ben_decode(*vec, strlen(*vec));
		if (success && b == NULL) {
			fprintf(stderr, "test vector %s failed. it should be valid.)\n", *vec);
			exit(1);
		}
		if (!success && b != NULL) {
			fprintf(stderr, "test vector %s failed. it should be invalid.\n", *vec);
			exit(1);
		}
		if (success)
			test_encode(b, *vec, strlen(*vec));
		ben_free(b);
		vec++;
	}
}

static void misc_tests(void)
{
	const char *validvectors[] = {
		"i4e",
		"i0e",
		"i-10e",
		"i9223372036854775807e",
		"i-9223372036854775808e",
		"0:",
		"3:abc",
		"10:1234567890",
		"le",
		"li1ei2ei3ee",
		"ll5:Alice3:Bobeli2ei3eee",
		"de",
		"d3:agei25e4:eyes4:bluee",
		"d8:spam.mp3d6:author5:Alice6:lengthi100000eee",
		"b0",
		"b1",
		"lb1i2ee",
		"li2eb0e",
		"l0:0:0:e",
		"l3:asd2:xye",
		"di1e0:e",
		NULL
	       };
	const char *invalidvectors[] = {
		"0:0:",
		"ie",
		"i341foo382e",
		"i 0e",
		"i-0e",
		"i123",
		"",
		"i6easd",
		"35208734823ljdahflajhdf",
		"2:abfdjslhfld",
		"02:xy",
		"d",
		"i",
		"l",
		"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
		"leanfdldjfh",
		"relwjhrlewjh",
		"defoobar",
		"d3:fooe",
		"d1:b0:1:a0:e", /* demand order for keys */
		"di1e0:i0e0:e", /* demand order for keys */
		"d1:a0:1:a0:e", /* disallow same two keys */
		"di0e0:i0e0:e", /* disallow same two keys */
		NULL
	       };
	testvectors(validvectors, 1);
	testvectors(invalidvectors, 0);
}

static void ben_dict_ordered_items_tests(void)
{
	struct bencode *d;
	struct bencode *key;
	struct bencode *value;
	struct bencode_keyvalue *pairs;
	const char *keyvalues[] = {
		"ka", "va", NULL,
		"ka", "va", NULL,
		"ka", "va", "kb", "vb", NULL,
		"ka", "va", "kb", "vb", NULL,
		"kb", "vb", "ka", "va", NULL,
		"ka", "va", "kb", "vb", NULL,
		"ka", "va", "kb", "vb", "kc", "vc", NULL,
		"ka", "va", "kb", "vb", "kc", "vc", NULL,
		"kb", "vb", "kc", "vc", "ka", "va", NULL,
		"ka", "va", "kb", "vb", "kc", "vc", NULL,
		NULL};
	const size_t n = sizeof(keyvalues) / sizeof(keyvalues[0]);
	size_t i;
	size_t j;
	size_t nkeys;

	d = ben_dict();
	pairs = ben_dict_ordered_items(d);
	assert(pairs != NULL);
	free(pairs);
	ben_free(d);

	i = 0;
	while (keyvalues[i] != NULL) {
		assert(i < n);
		d = ben_dict();
		nkeys = 0;
		while ((i + 1) < n && keyvalues[i] != NULL) {
			assert(keyvalues[i +1] != NULL);
			ben_dict_set_str_by_str(d, keyvalues[i], keyvalues[i + 1]);
			i += 2;
			nkeys++;
		}
		assert(keyvalues[i] == NULL);
		i++;

		pairs = ben_dict_ordered_items(d);

		j = 0;
		while ((i + 1) < n && keyvalues[i] != NULL) {
			assert(keyvalues[i + 1] != NULL);
			key = pairs[j].key;
			value = pairs[j].value;
			assert(!strcmp(ben_str_val(key), keyvalues[i]));
			assert(!strcmp(ben_str_val(value), keyvalues[i + 1]));
			j++;
			i += 2;
		}
		assert(keyvalues[i] == NULL);
		i++;

		assert(j == nkeys);
		free(pairs);
		pairs = NULL;
		ben_free(d);
		d = NULL;
	}
	assert(i == (n - 1));
}

static void dict_tests(void)
{
	struct bencode *d = ben_dict();
	struct bencode *i = ben_int(0);
	struct bencode *key2 = ben_str("key2");
	struct bencode *i2 = ben_int(0);
	struct bencode *key;
	struct bencode *value;
	size_t pos;

	assert(ben_dict_set_by_str(d, "key", i) == 0);
	assert(ben_dict_get_by_str(d, "key") != NULL);
	assert(ben_dict_set(d, key2, i2) == 0);
	assert(ben_dict_get(d, key2) != NULL);
	ben_free(d);

	d = ben_dict();
	assert(!ben_dict_set_str_by_str(d, "foo0", "a"));
	assert(!ben_dict_set_str_by_str(d, "bar0", "b"));
	assert(!ben_dict_set_str_by_str(d, "foo1", "c"));
	ben_dict_for_each(key, value, pos, d) {
		if (strncmp(ben_str_val(key), "foo", 3) != 0)
			ben_free(ben_dict_pop_current(d, &pos));
	}
	assert(ben_dict_len(d) == 2);
	ben_free(d);
}

static void dict_tests_2(void)
{
	struct bencode *d;
	int i;
	long long llkey;
	const int n = 50;
	size_t nkeys;
	size_t pos;
	struct bencode *key;
	struct bencode *value;

	srandom(666);
	d = ben_dict();
	for (i = 0; i < n; i++) {
		llkey = random();
		assert(ben_dict_set(d, ben_int(llkey), ben_str("foo")) == 0);
	}
	nkeys = 0;
	ben_dict_for_each(key, value, pos, d) {
		nkeys++;
	}
	assert(nkeys > 0 && nkeys <= n);
	ben_free(d);

	d = ben_dict();
	for (i = 0; i < n; i++) {
		llkey = i;
		assert(ben_dict_set(d, ben_int(llkey), ben_str("foo")) == 0);
	}
	nkeys = 0;
	ben_dict_for_each(key, value, pos, d) {
		nkeys++;
	}
	assert(nkeys == n);
	for (i = 0; i < n; i++) {
		value = ben_dict_get_by_int(d, i);
		assert(value != NULL);
	}
	for (i = 0; i < n; i++) {
		key = ben_int(i);
		value = ben_dict_pop(d, key);
		assert(value != NULL);
		ben_free(key);
		ben_free(value);
	}
	ben_free(d);

	d = ben_dict();
	for (i = 0; i < n; i++) {
		char skey[32];
		snprintf(skey, sizeof skey, "%d", i);
		assert(ben_dict_set_by_str(d, skey, ben_str("foo")) == 0);
	}
	nkeys = 0;
	ben_dict_for_each(key, value, pos, d) {
		nkeys++;
	}
	assert(nkeys == n);
	for (i = 0; i < n; i++) {
		char skey[32];
		snprintf(skey, sizeof skey, "%d", i);
		value = ben_dict_get_by_str(d, skey);
		assert(value != NULL);
	}
	for (i = 0; i < n; i++) {
		char skey[32];
		snprintf(skey, sizeof skey, "%d", i);
		value = ben_dict_pop_by_str(d, skey);
		assert(value != NULL);
		ben_free(value);
	}
	ben_free(d);
}

static void list_tests(void)
{
	struct bencode *l = ben_list();
	struct bencode *value;
	size_t pos;

	assert(!ben_list_append_str(l, "foo0"));
	assert(!ben_list_append_str(l, "bar0"));
	assert(!ben_list_append_str(l, "foo1"));
	assert(!ben_list_append_str(l, "foo2"));

	value = ben_list_pop(l, 3);
	assert(value != NULL);
	ben_free(value);
	assert(ben_list_len(l) == 3);

	ben_list_for_each(value, pos, l) {
		if (strncmp(ben_str_val(value), "foo", 3) != 0)
			ben_free(ben_list_pop_current(l, &pos));
	}
	assert(ben_list_len(l) == 2);
	ben_free(l);
}

static void decode_printed_tests(void)
{
	struct {
		char *s;
		size_t l;
		int e;
		int line;
		long long ival;
	} testcases[] = {
		/* string tests */
		{.s = "''", .l = 2},
		{.s = "''", .l = 2},
		{.s = "'a'", .l = 3},
		{.s = "'ab'", .l = 4},
		{.s = "'\0'", .l = 3, .e = BEN_INVALID},
		{.s = "'\\x00'", .l = 6},
		{.s = "'\\\\'", .l = 4},
		{.s = "'\\''", .l = 4},
		{.s = "'\"'", .l = 3},
		{.s = "\"'\"", .l = 3},
		{.s = "'", .l = 1, .e = BEN_INSUFFICIENT},
		{.s = "\"", .l = 1, .e = BEN_INSUFFICIENT},
		{.s = "'a\"", .l = 3, .e = BEN_INSUFFICIENT},

		/* list tests */
		{.s = "[]", .l = 2},
		{.s = "[,]", .l = 3, .e = BEN_INVALID},
		{.s = "[1]", .l = 3},
		{.s = "[1,]", .l = 4},
		{.s = "[1,2]", .l = 5},
		{.s = "[1,2,]", .l = 6},
		{.s = "['']", .l = 4},
		{.s = "[''] ", .l = 5},
		{.s = " ['']", .l = 5},

		/* int tests */
		{.s = "0", .l = 1, .ival = 0},
		{.s = " 0", .l = 2, .ival = 0},
		{.s = "0 ", .l = 2, .ival = 0},
		{.s = "-0", .l = 1, .e = BEN_INSUFFICIENT},
		{.s = "-0", .l = 2, .ival = 0},
		{.s = "-1", .l = 2, .ival = -1},
		{.s = "1", .l = 1, .ival = 1},
		{.s = "10", .l = 2, .ival = 10},
		{.s = "0x0", .l = 3, .ival = 0},
		{.s = "0x", .l = 2, .e = BEN_INSUFFICIENT},
		{.s = "-0x1", .l = 4, .ival = -1},
		{.s = "0xa", .l = 3, .ival = 10},
		{.s = "0x10", .l = 4, .ival = 16},
		{.s = "010", .l = 3, .ival = 8},
		{.s = "001", .l = 3, .ival = 1},
		{.s = "-010", .l = 4, .ival = -8},
		{.s = "0xk", .l = 3, .e = BEN_INVALID},
		{.s = "0x1000", .l = 6, .ival = 4096},

		/* bool tests */
		{.s = "T", .l = 1, .e = BEN_INSUFFICIENT},
		{.s = "Tr", .l = 2, .e = BEN_INSUFFICIENT},
		{.s = "Tru", .l = 3, .e = BEN_INSUFFICIENT},
		{.s = "True", .l = 4},
		{.s = "True ", .l = 4},
		{.s = "Truf", .l = 4, .e = BEN_INVALID},
		{.s = "F", .l = 1, .e = BEN_INSUFFICIENT},
		{.s = "Fa", .l = 2, .e = BEN_INSUFFICIENT},
		{.s = "Fal", .l = 3, .e = BEN_INSUFFICIENT},
		{.s = "Fals", .l = 4, .e = BEN_INSUFFICIENT},
		{.s = "False", .l = 5},
		{.s = "False ", .l = 6},
		{.s = "Falsf", .l = 5, .e = BEN_INVALID},

		/* dict tests */
		{.s = "{}", .l = 2},
		{.s = "{1: 2}", .l = 6},
		{.s = "{", .l = 1, .e = BEN_INSUFFICIENT},
		{.s = "{'", .l = 2, .e = BEN_INSUFFICIENT},
		{.s = "{'a", .l = 3, .e = BEN_INSUFFICIENT},
		{.s = "{'a'", .l = 4, .e = BEN_INSUFFICIENT},
		{.s = "{'a':", .l = 5, .e = BEN_INSUFFICIENT},
		{.s = "{'a': ", .l = 6, .e = BEN_INSUFFICIENT},
		{.s = "{'a': '", .l = 7, .e = BEN_INSUFFICIENT},
		{.s = "{'a': 'b", .l = 8, .e = BEN_INSUFFICIENT},
		{.s = "{'a': 'b'", .l = 9, .e = BEN_INSUFFICIENT},
		{.s = "{'a': 'b'}", .l = 10},
		{.s = "{'a': []}", .l = 9},
		{.s = "{'a': [1]}", .l = 10},

		/* Lexical tests */
		{.s = "{'a': 'b'\n}", .l = 11},
		{.s = "{'a': 'b',\n}", .l = 12},
		{.s = "{'a': 'b'\n'c'}", .l = 14, .e = BEN_INVALID, .line = 1},
		{.s = "{'a': 'b'\n\n'c'}", .l = 15, .e = BEN_INVALID, .line = 2},
		{.s = "#bar\n0x1000", .l = 11, .ival = 4096},
		{.s = "0x1000#foo\n", .l = 11, .ival = 4096},
		{.s = "#bar\nx0x1000", .l = 12, .ival = 4096, .e = BEN_INVALID, .line = 1},

		{.s = NULL}};
	int i;
	struct bencode_error err;
	size_t off;
	struct bencode *b;

	for (i = 0; testcases[i].s != NULL; i++) {
		off = 0;
		b = ben_decode_printed2(testcases[i].s, testcases[i].l, &off, &err);
		if (testcases[i].e != err.error) {
			fprintf(stderr, "Error in test case %s\n", testcases[i].s);
			exit(1);
		}

		if (testcases[i].line > 0 &&
		    (err.error == BEN_OK || err.line != testcases[i].line)) {
			fprintf(stderr, "Unexpected error line in test case %s. Expected line %d but got %d\n", testcases[i].s, testcases[i].line, err.line);

		}

		if (err.error == 0 && ben_is_int(b) &&
		    ben_int_val(b) != testcases[i].ival) {
			fprintf(stderr, "Invalid result value in test case %s. Got %lld, but expected %lld\n", testcases[i].s, ben_int_val(b), testcases[i].ival);
			exit(1);
		}

		ben_free(b);
		b = NULL;
	}
}

static void clone_tests(void)
{
	struct bencode *b;
	struct bencode *c;

	b = ben_list();
	ben_list_append(b, ben_int(1));
	ben_list_append(b, ben_int(1));

	c = ben_clone(b);
	assert(ben_list_len(c) == 2);
	ben_free(c);

	c = ben_shared_clone(b);
	assert(ben_list_len(c) == 2);
	ben_free(c);

	ben_free(b);

	b = ben_dict();
	ben_dict_set_str_by_str(b, "foo0", "a");
	ben_dict_set_str_by_str(b, "foo1", "b");
	ben_dict_set_str_by_str(b, "foo2", "c");

	c = ben_clone(b);
	assert(ben_dict_get_by_str(c, "foo0") != NULL);
	assert(ben_dict_get_by_str(c, "foo1") != NULL);
	assert(ben_dict_get_by_str(c, "foo2") != NULL);
	assert(ben_dict_len(c) == 3);
	ben_free(c);

	c = ben_shared_clone(b);
	assert(ben_dict_get_by_str(c, "foo0") != NULL);
	assert(ben_dict_get_by_str(c, "foo1") != NULL);
	assert(ben_dict_get_by_str(c, "foo2") != NULL);
	assert(ben_dict_len(c) == 3);
	ben_free(c);

	ben_free(b);

	b = ben_int(666);
	c = ben_clone(b);
	assert(ben_int_val(c) == ben_int_val(b));
	ben_free(c);
	ben_free(b);

	b = ben_str("foo");
	c = ben_clone(b);
	if (strcmp(ben_str_val(b), ben_str_val(c)) != 0) {
		fprintf(stderr, "Cloned strings are different\n");
		abort();
	}
	ben_free(c);
	ben_free(b);

	b = ben_bool(1);
	c = ben_clone(b);
	assert(ben_bool_val(b) == ben_bool_val(c));
	ben_free(c);
	ben_free(b);
}

static void alloc_tests(void)
{
	struct bencode *list;
	struct bencode *dict;

	list = ben_list();
	assert(ben_allocate(list, 1) == 0);
	assert(ben_list_cast(list)->alloc == 1);
	assert(ben_allocate(list, 64) == 0);
	assert(ben_list_cast(list)->alloc == 64);
	assert(ben_allocate(list, 1) == 0);
	assert(ben_list_cast(list)->alloc == 1);
	ben_free(list);

	dict = ben_dict();
	assert(ben_allocate(dict, 1) == 0);
	assert(ben_dict_cast(dict)->alloc == 1);
	assert(ben_allocate(dict, 64) == 0);
	assert(ben_dict_cast(dict)->alloc == 64);
	assert(ben_allocate(dict, 1) == 0);
	assert(ben_dict_cast(dict)->alloc == 1);
	ben_free(dict);
}

#define SHA1_LEN		20

struct ben_sha1 {
	struct bencode_user user;
	char sha1[SHA1_LEN];
};

struct ben_pair {
	struct bencode_user user;
	struct bencode *first;
	struct bencode *second;
};

static struct bencode_type sha1_type;
static struct bencode_type pair_type;

static struct bencode *alloc_sha1(const char *data)
{
	struct ben_sha1 *sha1 = ben_alloc_user(&sha1_type);
	if (sha1 == NULL)
		return NULL;
	memcpy(sha1->sha1, data, SHA1_LEN);
	return (struct bencode *) sha1;
}

static struct bencode *sha1_decode(struct ben_decode_ctx *ctx)
{
	struct bencode *sha1;
	const char *data = ben_current_buf(ctx, SHA1_LEN);
	if (data == NULL)
		return ben_insufficient_ptr(ctx);

	sha1 = alloc_sha1(data);
	if (sha1 == NULL)
		return ben_oom_ptr(ctx);

	ben_skip(ctx, SHA1_LEN);
	return sha1;
}

static int sha1_encode(struct ben_encode_ctx *ctx, const struct bencode *b)
{
	const struct ben_sha1 *sha1 = ben_user_type_const_cast(b, &sha1_type);

	if (ben_put_char(ctx, 'r'))
		return -1;

	return ben_put_buffer(ctx, sha1->sha1, SHA1_LEN);
}

static size_t sha1_get_size(const struct bencode *b)
{
	(void) b;
	return 1 + SHA1_LEN;
}

static struct bencode *alloc_pair(struct bencode *first, struct bencode *second)
{
	struct ben_pair *pair = ben_alloc_user(&pair_type);
	if (pair == NULL)
		return NULL;
	pair->first = first;
	pair->second = second;
	return (struct bencode *) pair;
}

static struct bencode *pair_decode(struct ben_decode_ctx *ctx)
{
	struct bencode *pair;
	struct bencode *second;
	struct bencode *first = ben_ctx_decode(ctx);
	if (first == NULL)
		return NULL;

	second = ben_ctx_decode(ctx);
	if (second == NULL) {
		ben_free(first);
		return NULL;
	}

	pair = alloc_pair(first, second);
	if (pair == NULL) {
		ben_free(first);
		ben_free(second);
		return ben_oom_ptr(ctx);
	}
	return (struct bencode *) pair;
}

static int pair_encode(struct ben_encode_ctx *ctx, const struct bencode *b)
{
	const struct ben_pair *pair = ben_user_type_const_cast(b, &pair_type);

	if (ben_put_char(ctx, 'p'))
		return -1;
	if (ben_ctx_encode(ctx, pair->first))
		return -1;
	if (ben_ctx_encode(ctx, pair->second))
		return -1;
	return 0;
}

static size_t pair_get_size(const struct bencode *b)
{
	const struct ben_pair *pair = ben_user_type_const_cast(b, &pair_type);

	return 1 + ben_encoded_size(pair->first) +
	       ben_encoded_size(pair->second);
}

static void pair_free(struct bencode *b)
{
	struct ben_pair *pair = ben_user_type_cast(b, &pair_type);

	ben_free(pair->first);
	ben_free(pair->second);
}

static struct bencode_type sha1_type = {
	.size = sizeof(struct ben_sha1),
	.decode = sha1_decode,
	.encode = sha1_encode,
	.get_size = sha1_get_size,
};

static struct bencode_type pair_type = {
	.size = sizeof(struct ben_pair),
	.decode = pair_decode,
	.encode = pair_encode,
	.get_size = pair_get_size,
	.free = pair_free,
};

static void user_tests(void)
{
	const char dummy_sha1[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
		0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13};
	size_t off = 0;
	size_t len = 0;
	struct bencode *b;
	struct bencode *c;
	struct bencode *d;
	struct ben_sha1 *sha1;
	struct ben_pair *pair;
	void *data;
	struct bencode_type *types[128] = {NULL};

	assert(sizeof dummy_sha1 == SHA1_LEN);

	types['r'] = &sha1_type;
	types['p'] = &pair_type;

	b = alloc_sha1(dummy_sha1);
	data = ben_encode(&len, b);
	ben_free(b);

	b = ben_decode3(data, len, &off, NULL, types);
	assert(ben_is_user_type(b, &sha1_type));

	sha1 = ben_user_type_cast(b, &sha1_type);
	assert(memcmp(sha1->sha1, dummy_sha1, SHA1_LEN) == 0);

	ben_free(b);
	free(data);

	off = 0;
	b = ben_decode3("r\00\00\00\00", 5, &off, NULL, types);
	assert(b == NULL);

	c = ben_str("foo");
	d = ben_int(1234);
	b = alloc_pair(c, d);
	data = ben_encode(&len, b);
	ben_free(b);

	assert(len == 12 && memcmp(data, "p3:fooi1234e", 12) == 0);

	off = 0;
	b = ben_decode3(data, len, &off, NULL, types);
	assert(ben_is_user_type(b, &pair_type));

	pair = ben_user_type_cast(b, &pair_type);
	assert(strcmp(ben_str_val(pair->first), "foo") == 0 &&
	       ben_int_val(pair->second) == 1234);

	ben_free(b);
	free(data);

	off = 0;
	b = ben_decode3("p3:foo", 6, &off, NULL, types);
	assert(b == NULL);
}

static void cmptest(const struct bencode *a, struct bencode *b, int expect)
{
	int ret = ben_cmp(a, b);
	if (ret != expect) {
		fprintf(stderr, "cmp returned %d, should have returned %d\n",
			ret, expect);
		abort();
	}
	if (ret) {
		/* Verify that comparison always gives the same order */
		ret = ben_cmp(b, a);
		if (ret != -expect) {
			fprintf(stderr, "cmp returned %d, should have returned %d\n",
				ret, -expect);
			abort();
		}
	}
	ben_free(b);
}

static void cmp_tests(void)
{
	struct bencode *b;
	struct bencode *c;
	struct bencode *d;

	b = ben_str("foo");
	cmptest(b, ben_str("foo"), 0);
	cmptest(b, ben_str("fooa"), -1);
	cmptest(b, ben_str("faa"), 1);
	ben_free(b);

	b = ben_list();
	ben_list_append(b, ben_str("a"));
	ben_list_append(b, ben_str("b"));

	c = ben_list();
	ben_list_append(c, ben_str("a"));
	ben_list_append(c, ben_str("b"));
	cmptest(b, c, 0);

	c = ben_list();
	ben_list_append(c, ben_str("a"));
	ben_list_append(c, ben_str("c"));
	cmptest(b, c, -1);

	c = ben_list();
	ben_list_append(c, ben_str("a"));
	ben_list_append(c, ben_str("b"));
	ben_list_append(c, ben_str("c"));
	cmptest(b, c, -1);

	c = ben_list();
	ben_list_append(c, ben_str("a"));
	ben_list_append(c, ben_str("a"));
	cmptest(b, c, 1);

	c = ben_list();
	ben_list_append(c, ben_str("a"));
	cmptest(b, c, 1);

	ben_free(b);

	b = ben_dict();
	ben_dict_set_str_by_str(b, "foo0", "a");
	ben_dict_set_str_by_str(b, "foo1", "b");

	c = ben_dict();
	ben_dict_set_str_by_str(c, "foo0", "a");
	ben_dict_set_str_by_str(c, "foo1", "b");
	cmptest(b, c, 0);

	c = ben_dict();
	ben_dict_set_str_by_str(c, "foo1", "c");
	ben_dict_set_str_by_str(c, "foo0", "a");
	cmptest(b, c, -1);

	c = ben_dict();
	ben_dict_set_str_by_str(c, "foo0", "a");
	ben_dict_set_str_by_str(c, "foo1", "b");
	ben_dict_set_str_by_str(c, "foo1", "c");
	cmptest(b, c, -1);

	c = ben_dict();
	ben_dict_set_str_by_str(c, "foo0", "a");
	ben_dict_set_str_by_str(c, "foo1", "a");
	cmptest(b, c, 1);

	c = ben_dict();
	ben_dict_set_str_by_str(c, "foo0", "a");
	cmptest(b, c, 1);

	c = ben_dict();
	ben_dict_set_str_by_str(c, "1", "c");
	ben_dict_set_str_by_str(c, "0", "b");
	d = ben_dict();
	ben_dict_set_str_by_str(d, "0", "a");
	ben_dict_set_str_by_str(d, "1", "d");
	cmptest(c, d, 1);

	ben_free(b);
}

int main(void)
{
	assert(ben_decode("i0e ", 4) == NULL);

	booltest("b0", 1, 0, BEN_INSUFFICIENT);
	booltest("b0", 2, 0, BEN_OK);
	booltest("b0 ", 3, 0, BEN_OK);
	booltest("b1", 2, 1, BEN_OK);
	booltest("b2", 2, 1, BEN_INVALID);

	inttest("i-1e", 4, -1, BEN_OK);
	inttest("i-1e", 3, -1, BEN_INSUFFICIENT);
	inttest("i0e", 3, 0, BEN_OK);
	inttest("i1e", 3, 1, BEN_OK);
	inttest("ie", 2, 0, BEN_INVALID);
	inttest("i1ke", 4, 1, BEN_INVALID);
	inttest("i123456789e", 11, 123456789, BEN_OK);

	strtest("0", 1, "", BEN_INSUFFICIENT);
	strtest("0:", 1, "", BEN_INSUFFICIENT);
	strtest("0:", 2, "", BEN_OK);
	strtest("0e", 2, "", BEN_INSUFFICIENT);
	strtest("0e:", 3, "", BEN_INVALID);
	strtest("7:marklar", 9, "marklar", BEN_OK);
	strtest("7:marklar", 8, "marklar", BEN_INSUFFICIENT);

	listtest("le", 2, 0, BEN_OK);
	listtest("lfe", 3, 0, BEN_INVALID);
	listtest("li0ee", 5, 1, BEN_OK);
	listtest("li0e", 4, 1, BEN_INSUFFICIENT);
	listtest("li0ei", 5, 1, BEN_INSUFFICIENT);
	listtest("li0ei1", 6, 1, BEN_INSUFFICIENT);
	listtest("li0ei1e", 7, 1, BEN_INSUFFICIENT);
	listtest("li0ei", 5, 1, BEN_INSUFFICIENT);
	listtest("li0ef", 5, 1, BEN_INVALID);
	listtest("li0ei0ee", 8, 2, BEN_OK);
	listtest("li0ei0ei0ee", 11, 3, BEN_OK);
	listtest("li0ei0ei0ei0ee", 14, 4, BEN_OK);
	listtest("li0ei0ei0ei0ei0ee", 17, 5, BEN_OK);
	listtest("l7:marklare", 11, 1, BEN_OK);
	listtest("l7:marklari0ee", 14, 2, BEN_OK);
	listtest("l7:marklarf", 11, 1, BEN_INVALID);

	dicttest("d", 1, 1, BEN_INSUFFICIENT);
	dicttest("di0e7:marklare", 14, 1, BEN_OK);
	dicttest("di0e", 4, 1, BEN_INSUFFICIENT);
	dicttest("di0e7", 5, 1, BEN_INSUFFICIENT);
	dicttest("di0e7:", 6, 1, BEN_INSUFFICIENT);
	dicttest("di0e7:marklar", 13, 1, BEN_INSUFFICIENT);
	dicttest("di0e7:marklare", 13, 1, BEN_INSUFFICIENT);
	dicttest("di0e7:marklari1e7:marklare", 26, 2, BEN_OK);
	dicttest("de", 2, 0, BEN_OK);

	encoded_size_tests();

	misc_tests();

	print_tests();

	dict_tests();
	dict_tests_2();
	ben_dict_ordered_items_tests();

	list_tests();

	decode_printed_tests();

	clone_tests();

	alloc_tests();

	user_tests();

	cmp_tests();

	return 0;
}
