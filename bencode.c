#include <bencodetools/bencode.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>

#define die(fmt, args...) do { fprintf(stderr, "bencode: fatal error: " fmt, ## args); abort(); } while (0)
#define warn(fmt, args...) do { fprintf(stderr, "bencode: warning: " fmt, ## args); } while (0)

#define MAX_ALLOC (((size_t) -1) / sizeof(struct bencode *) / 2)
#define DICT_MAX_ALLOC (((size_t) -1) / sizeof(struct bencode_dict_node) / 2)

struct ben_decode_ctx {
	const char *data;
	const size_t len;
	size_t off;
	int error;
	int level;
	char c;
	int line;
	struct bencode_type **types;
};

struct ben_encode_ctx {
	char *data;
	size_t size;
	size_t pos;
};

/*
 * Buffer size for fitting all unsigned long long and long long integers,
 * assuming it is at most 64 bits. If long long is larger than 64 bits,
 * an error is produced when too large an integer is converted.
 */
#define LONGLONGSIZE 21

struct bencode_keyvalue {
	struct bencode *key;
	struct bencode *value;
};

static struct bencode *decode(struct ben_decode_ctx *ctx);
static struct bencode *decode_printed(struct ben_decode_ctx *ctx);
static int resize_dict(struct bencode_dict *d, size_t newalloc);
static int resize_list(struct bencode_list *list, size_t newalloc);

static size_t type_size(int type)
{
	switch (type) {
	case BENCODE_BOOL:
		return sizeof(struct bencode_bool);
	case BENCODE_DICT:
		return sizeof(struct bencode_dict);
	case BENCODE_INT:
		return sizeof(struct bencode_int);
	case BENCODE_LIST:
		return sizeof(struct bencode_list);
	case BENCODE_STR:
		return sizeof(struct bencode_str);
	default:
		die("Unknown type: %d\n", type);
	}
}

static void *alloc(int type)
{
	struct bencode *b = calloc(1, type_size(type));
	if (b == NULL)
		return NULL;
	b->type = type;
	return b;
}

void *ben_alloc_user(struct bencode_type *type)
{
	struct bencode_user *user = calloc(1, type->size);
	if (user == NULL)
		return NULL;
	user->type = BENCODE_USER;
	user->info = type;
	return user;
}

static int insufficient(struct ben_decode_ctx *ctx)
{
	ctx->error = BEN_INSUFFICIENT;
	return -1;
}

static int invalid(struct ben_decode_ctx *ctx)
{
	ctx->error = BEN_INVALID;
	return -1;
}

void *ben_insufficient_ptr(struct ben_decode_ctx *ctx)
{
	ctx->error = BEN_INSUFFICIENT;
	return NULL;
}

void *ben_invalid_ptr(struct ben_decode_ctx *ctx)
{
	ctx->error = BEN_INVALID;
	return NULL;
}

void *ben_oom_ptr(struct ben_decode_ctx *ctx)
{
	ctx->error = BEN_NO_MEMORY;
	return NULL;
}

int ben_need_bytes(const struct ben_decode_ctx *ctx, size_t n)
{
	return ((ctx->off + n) <= ctx->len) ? 0 : -1;
}

char ben_current_char(const struct ben_decode_ctx *ctx)
{
	return ctx->data[ctx->off];
}

const char *ben_current_buf(const struct ben_decode_ctx *ctx, size_t n)
{
	if (ben_need_bytes(ctx, n))
		return NULL;
	return ctx->data + ctx->off;
}

void ben_skip(struct ben_decode_ctx *ctx, size_t n)
{
	ctx->off += n;
}

static struct bencode *internal_blob(void *data, size_t len)
{
	struct bencode_str *b = alloc(BENCODE_STR);
	if (b == NULL)
		return NULL;
	b->s = data;
	b->len = len;
	assert(b->s[len] == 0);
	return (struct bencode *) b;
}

static void skip_to_next_line(struct ben_decode_ctx *ctx)
{
	for (; ctx->off < ctx->len; ctx->off++) {
		if (ben_current_char(ctx) == '\n') {
			ctx->line++;
			ctx->off++;
			break;
		}
	}
}

static int seek_char(struct ben_decode_ctx *ctx)
{
	while (ctx->off < ctx->len) {
		char c = ben_current_char(ctx);
		if (isspace(c)) {
			if (c == '\n')
				ctx->line++;
			ctx->off++;
		} else if (c == '#') {
			/* Skip comment */
			ctx->off++;
			skip_to_next_line(ctx);
		} else {
			return 0;
		}
	}
	return insufficient(ctx);
}

/*
 * Test if string 's' is located at current position.
 * Increment current position and return 0 if the string matches.
 * Returns -1 otherwise. The function avoids buffer overflow.
 */
static int try_match(struct ben_decode_ctx *ctx, const char *s)
{
	size_t n = strlen(s);
	if (ben_need_bytes(ctx, n))
		return -1;
	if (memcmp(ctx->data + ctx->off, s, n) != 0)
		return -1;
	ctx->off += n;
	return 0;
}

static int try_match_with_errors(struct ben_decode_ctx *ctx, const char *s)
{
	size_t n = strlen(s);
	size_t left = ctx->len - ctx->off;

	assert(ctx->off <= ctx->len);

	if (left == 0)
		return insufficient(ctx);

	if (left < n) {
		if (memcmp(ctx->data + ctx->off, s, left) != 0)
			return invalid(ctx);
		return insufficient(ctx);
	}

	if (memcmp(ctx->data + ctx->off, s, n) != 0)
		return invalid(ctx);

	ctx->off += n;
	return 0;
}

int ben_allocate(struct bencode *b, size_t n)
{
	switch (b->type) {
	case BENCODE_DICT:
		return resize_dict(ben_dict_cast(b), n);
	case BENCODE_LIST:
		return resize_list(ben_list_cast(b), n);
	default:
		die("ben_allocate(): Unknown type %d\n", b->type);
	}
}

static struct bencode *clone_dict(const struct bencode_dict *d)
{
	struct bencode *key;
	struct bencode *value;
	struct bencode *newkey;
	struct bencode *newvalue;
	size_t pos;
	struct bencode *newdict = ben_dict();
	if (newdict == NULL)
		return NULL;
	ben_dict_for_each(key, value, pos, d) {
		newkey = ben_clone(key);
		newvalue = ben_clone(value);
		if (newkey == NULL || newvalue == NULL) {
			ben_free(newkey);
			ben_free(newvalue);
			goto error;
		}
		if (ben_dict_set(newdict, newkey, newvalue)) {
			ben_free(newkey);
			ben_free(newvalue);
			goto error;
		}
		newkey = NULL;
		newvalue = NULL;
	}
	return (struct bencode *) newdict;

error:
	ben_free(newdict);
	return NULL;
}

static struct bencode *clone_list(const struct bencode_list *list)
{
	struct bencode *value;
	struct bencode *newvalue;
	size_t pos;
	struct bencode *newlist = ben_list();
	if (newlist == NULL)
		return NULL;
	ben_list_for_each(value, pos, list) {
		newvalue = ben_clone(value);
		if (newvalue == NULL)
			goto error;
		if (ben_list_append(newlist, newvalue)) {
			ben_free(newvalue);
			goto error;
		}
		newvalue = NULL;
	}
	return (struct bencode *) newlist;

error:
	ben_free(newlist);
	return NULL;
}

static struct bencode *clone_str(const struct bencode_str *s)
{
	return ben_blob(s->s, s->len);
}

struct bencode *ben_clone(const struct bencode *b)
{
	switch (b->type) {
	case BENCODE_BOOL:
		return ben_bool(ben_bool_const_cast(b)->b);
	case BENCODE_DICT:
		return clone_dict(ben_dict_const_cast(b));
	case BENCODE_INT:
		return ben_int(ben_int_const_cast(b)->ll);
	case BENCODE_LIST:
		return clone_list(ben_list_const_cast(b));
	case BENCODE_STR:
		return clone_str(ben_str_const_cast(b));
	default:
		die("Invalid type %c\n", b->type);
	}	
}

int ben_cmp(const struct bencode *a, const struct bencode *b)
{
	size_t cmplen;
	int ret;
	const struct bencode_str *sa;
	const struct bencode_str *sb;

	if (a->type != b->type)
		return (a->type == BENCODE_INT) ? -1 : 1;

	if (a->type == BENCODE_INT) {
		const struct bencode_int *ia = ben_int_const_cast(a);
		const struct bencode_int *ib = ben_int_const_cast(b);
		if (ia->ll < ib->ll)
			return -1;
		if (ib->ll < ia->ll)
			return 1;
		return 0;
	}

	sa = ben_str_const_cast(a);
	sb = ben_str_const_cast(b);
	cmplen = (sa->len <= sb->len) ? sa->len : sb->len;
	ret = memcmp(sa->s, sb->s, cmplen);
	if (sa->len == sb->len)
		return ret;
	if (ret)
		return ret;
	return (sa->len < sb->len) ? -1 : 1;
}

int ben_cmp_qsort(const void *a, const void *b)
{
	const struct bencode *akey = ((const struct bencode_keyvalue *) a)->key;
	const struct bencode *bkey = ((const struct bencode_keyvalue *) b)->key;
	return ben_cmp(akey, bkey);
}

static struct bencode *decode_bool(struct ben_decode_ctx *ctx)
{
	struct bencode_bool *b;
	char value;
	char c;
	if (ben_need_bytes(ctx, 2))
		return ben_insufficient_ptr(ctx);
	ctx->off += 1;

	c = ben_current_char(ctx);
	if (c != '0' && c != '1')
		return ben_invalid_ptr(ctx);

	value = (c == '1');
	b = alloc(BENCODE_BOOL);
	if (b == NULL)
		return ben_oom_ptr(ctx);

	b->b = value;
	ctx->off += 1;
	return (struct bencode *) b;
}

static size_t hash_bucket(long long hash, const struct bencode_dict *d)
{
	return hash & (d->alloc - 1);
}

static size_t hash_bucket_head(long long hash, const struct bencode_dict *d)
{
	if (d->buckets == NULL)
		return -1;
	return d->buckets[hash_bucket(hash, d)];
}

static int resize_dict(struct bencode_dict *d, size_t newalloc)
{
	size_t *newbuckets;
	struct bencode_dict_node *newnodes;;
	size_t pos;

	if (newalloc == -1) {
		if (d->alloc >= DICT_MAX_ALLOC)
			return -1;

		if (d->alloc == 0)
			newalloc = 4;
		else
			newalloc = d->alloc * 2;
	} else {
		size_t x;
		if (newalloc < d->n || newalloc > DICT_MAX_ALLOC)
			return -1;
		/* Round to next power of two */
		x = 1;
		while (x < newalloc)
			x <<= 1;
		assert(x >= newalloc);
		newalloc = x;
		if (newalloc > DICT_MAX_ALLOC)
			return -1;		
	}

	/* size must be a power of two */
	assert((newalloc & (newalloc - 1)) == 0);

	newbuckets = realloc(d->buckets, sizeof(newbuckets[0]) * newalloc);
	newnodes = realloc(d->nodes, sizeof(newnodes[0]) * newalloc);
	if (newnodes == NULL || newbuckets == NULL) {
		free(newnodes);
		free(newbuckets);
		return -1;
	}

	d->alloc = newalloc;
	d->buckets = newbuckets;
	d->nodes = newnodes;

	/* Clear all buckets */
	memset(d->buckets, -1, d->alloc * sizeof(d->buckets[0]));

	/* Reinsert nodes into buckets */
	for (pos = 0; pos < d->n; pos++) {
		struct bencode_dict_node *node = &d->nodes[pos];
		size_t bucket = hash_bucket(node->hash, d);
		node->next = d->buckets[bucket];
		d->buckets[bucket] = pos;
	}

	return 0;
}

/* The string/binary object hash is copied from Python */
static long long str_hash(const unsigned char *s, size_t len)
{
	long long hash;
	size_t i;
	if (len == 0)
		return 0;
	hash = s[0] << 7;
	for (i = 0; i < len; i++)
		hash = (1000003 * hash) ^ s[i];
	hash ^= len;
	if (hash == -1)
		hash = -2;
	return hash;
}

long long ben_str_hash(const struct bencode *b)
{
	const struct bencode_str *bstr = ben_str_const_cast(b);
	const unsigned char *s = (unsigned char *) bstr->s;
	return str_hash(s, bstr->len);
}

long long ben_int_hash(const struct bencode *b)
{
	long long x = ben_int_const_cast(b)->ll;
	return (x == -1) ? -2 : x;
}

long long ben_hash(const struct bencode *b)
{
	switch (b->type) {
	case BENCODE_INT:
		return ben_int_hash(b);
	case BENCODE_STR:
		return ben_str_hash(b);
	default:
		die("hash: Invalid type: %d\n", b->type);
	}		
}

static struct bencode *decode_dict(struct ben_decode_ctx *ctx)
{
	struct bencode *key;
	struct bencode *lastkey = NULL;
	struct bencode *value;
	struct bencode_dict *d;

	d = alloc(BENCODE_DICT);
	if (d == NULL) {
		warn("Not enough memory for dict\n");
		return ben_oom_ptr(ctx);
	}

	ctx->off += 1;

	while (ctx->off < ctx->len && ben_current_char(ctx) != 'e') {
		if (d->n == d->alloc && resize_dict(d, -1)) {
			warn("Can not resize dict\n");
			ctx->error = BEN_NO_MEMORY;
			goto error;
		}
		key = decode(ctx);
		if (key == NULL)
			goto error;
		if (key->type != BENCODE_INT && key->type != BENCODE_STR) {
			ben_free(key);
			key = NULL;
			ctx->error = BEN_INVALID;
			warn("Invalid dict key type\n");
			goto error;
		}

		if (lastkey != NULL && ben_cmp(lastkey, key) != -1) {
			ben_free(key);
			key = NULL;
			ctx->error = BEN_INVALID;
			goto error;
		}

		value = decode(ctx);
		if (value == NULL) {
			ben_free(key);
			key = NULL;
			goto error;
		}

		ben_dict_set((struct bencode *) d, key, value);
		lastkey = key;
	}
	if (ctx->off >= ctx->len) {
		ctx->error = BEN_INSUFFICIENT;
		goto error;
	}

	ctx->off += 1;

	return (struct bencode *) d;

error:
	ben_free((struct bencode *) d);
	return NULL;
}

static size_t find(const struct ben_decode_ctx *ctx, char c)
{
	char *match = memchr(ctx->data + ctx->off, c, ctx->len - ctx->off);
	if (match == NULL)
		return -1;
	return (size_t) (match - ctx->data);
}

/* off is the position of first number in */
static int read_long_long(long long *ll, struct ben_decode_ctx *ctx, int c)
{
	char buf[LONGLONGSIZE]; /* fits all 64 bit integers */
	char *endptr;
	size_t slen;
	size_t pos = find(ctx, c);

	if (pos == -1)
		return insufficient(ctx);

	slen = pos - ctx->off;
	if (slen == 0 || slen >= sizeof buf)
		return invalid(ctx);

	assert(slen < sizeof buf);
	memcpy(buf, ctx->data + ctx->off, slen);
	buf[slen] = 0;

	if (buf[0] != '-' && !isdigit(buf[0]))
		return invalid(ctx);

	errno = 0;
	*ll = strtoll(buf, &endptr, 10);
	if (errno == ERANGE || *endptr != 0)
		return invalid(ctx);

	/*
	 * Demand a unique encoding for all integers.
	 * Zero may not begin with a (minus) sign.
	 * Non-zero integers may not have leading zeros in the encoding.
	 */
	if (buf[0] == '-' && buf[1] == '0')
		return invalid(ctx);
	if (buf[0] == '0' && pos != (ctx->off + 1))
		return invalid(ctx);

	ctx->off = pos + 1;
	return 0;
}

static struct bencode *decode_int(struct ben_decode_ctx *ctx)
{
	struct bencode_int *b;
	long long ll;
	ctx->off += 1;
	if (read_long_long(&ll, ctx, 'e'))
		return NULL;
	b = alloc(BENCODE_INT);
	if (b == NULL)
		return ben_oom_ptr(ctx);
	b->ll = ll;
	return (struct bencode *) b;
}

static int resize_list(struct bencode_list *list, size_t newalloc)
{
	struct bencode **newvalues;
	size_t newsize;

	if (newalloc == -1) {
		if (list->alloc >= MAX_ALLOC)
			return -1;
		if (list->alloc == 0)
			newalloc = 4;
		else
			newalloc = list->alloc * 2;
	} else {
		if (newalloc < list->n || newalloc > MAX_ALLOC)
			return -1;
	}

	newsize = sizeof(list->values[0]) * newalloc;
	newvalues = realloc(list->values, newsize);
	if (newvalues == NULL)
		return -1;
	list->alloc = newalloc;
	list->values = newvalues;
	return 0;
}

static struct bencode *decode_list(struct ben_decode_ctx *ctx)
{
	struct bencode_list *l = alloc(BENCODE_LIST);
	if (l == NULL)
		return ben_oom_ptr(ctx);

	ctx->off += 1;

	while (ctx->off < ctx->len && ben_current_char(ctx) != 'e') {
		struct bencode *b = decode(ctx);
		if (b == NULL)
			goto error;
		if (ben_list_append((struct bencode *) l, b)) {
			ben_free(b);
			ctx->error = BEN_NO_MEMORY;
			goto error;
		}
	}

	if (ctx->off >= ctx->len) {
		ctx->error = BEN_INSUFFICIENT;
		goto error;
	}

	ctx->off += 1;
	return (struct bencode *) l;

error:
	ben_free((struct bencode *) l);
	return NULL;
}

static size_t read_size_t(struct ben_decode_ctx *ctx, int c)
{
	long long ll;
	size_t s;
	if (read_long_long(&ll, ctx, c))
		return -1;
	if (ll < 0)
		return invalid(ctx);
	/*
	 * Test that information is not lost when converting from long long
	 * to size_t
	 */
	s = (size_t) ll;
	if (ll != (long long) s)
		return invalid(ctx);
	return s;
}

static struct bencode *decode_str(struct ben_decode_ctx *ctx)
{
	struct bencode *b;
	size_t datalen = read_size_t(ctx, ':'); /* Read the string length */
	if (datalen == -1)
		return NULL;

	if (ben_need_bytes(ctx, datalen))
		return ben_insufficient_ptr(ctx);

	/* Allocate string structure and copy data into it */
	b = ben_blob(ctx->data + ctx->off, datalen);
	ctx->off += datalen;
	return b;
}

static struct bencode *decode(struct ben_decode_ctx *ctx)
{
	char c;
	struct bencode_type *type;
	struct bencode *b;
	ctx->level++;
	if (ctx->level > 256)
		return ben_invalid_ptr(ctx);

	if (ctx->off == ctx->len)
		return ben_insufficient_ptr(ctx);

	assert (ctx->off < ctx->len);
	c = ben_current_char(ctx);
	switch (c) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		b = decode_str(ctx);
		break;
	case 'b':
		b = decode_bool(ctx);
		break;
	case 'd':
		b = decode_dict(ctx);
		break;
	case 'i':
		b = decode_int(ctx);
		break;
	case 'l':
		b = decode_list(ctx);
		break;
	default:
		if (ctx->types && (unsigned char) c < 128) {
			type = ctx->types[(unsigned char) c];
			if (type) {
				ctx->off += 1;
				b = type->decode(ctx);
			} else
				return ben_invalid_ptr(ctx);
		} else
			return ben_invalid_ptr(ctx);
	}
	ctx->level--;
	return b;
}

struct bencode *ben_decode(const void *data, size_t len)
{
	struct ben_decode_ctx ctx = {.data = data, .len = len};
	struct bencode *b = decode(&ctx);
	if (b != NULL && ctx.off != len) {
		ben_free(b);
		return NULL;
	}
	return b;
}

struct bencode *ben_decode2(const void *data, size_t len, size_t *off, int *error)
{
	struct ben_decode_ctx ctx = {.data = data, .len = len, .off = *off};
	struct bencode *b = decode(&ctx);
	*off = ctx.off;
	if (error != NULL) {
		assert((b != NULL) ^ (ctx.error != 0));
		*error = ctx.error;
	}
	return b;
}

struct bencode *ben_decode3(const void *data, size_t len, size_t *off, int *error, struct bencode_type *types[128])
{
	struct ben_decode_ctx ctx = {.data = data, .len = len, .off = *off,
				     .types = types};
	struct bencode *b = decode(&ctx);
	*off = ctx.off;
	if (error != NULL) {
		assert((b != NULL) ^ (ctx.error != 0));
		*error = ctx.error;
	}
	return b;
}

static struct bencode *decode_printed_bool(struct ben_decode_ctx *ctx)
{
	struct bencode *b;
	int bval = -1;

	if (try_match(ctx, "True")) {	
		if (ben_need_bytes(ctx, 4))
			return ben_insufficient_ptr(ctx);
	} else {
		bval = 1;
	}

	if (bval < 0) {
		/* It's not 'True', so it can only be 'False'. Verify it. */
		if (try_match_with_errors(ctx, "False"))
			return NULL;
		bval = 0;
	}

	assert(bval == 0 || bval == 1);
	b = ben_bool(bval);
	if (b == NULL)
		return ben_oom_ptr(ctx);
	return b;
}

static struct bencode *decode_printed_dict(struct ben_decode_ctx *ctx)
{
	struct bencode *d = ben_dict();
	int dictstate = 0; /* 0 == key, 1 == colon, 2 == value, 3 == comma */
	struct bencode *key = NULL;
	struct bencode *value = NULL;

	if (d == NULL)
		return ben_oom_ptr(ctx);

	ctx->off++;

	while (1) {
		if (seek_char(ctx))
			goto nullpath;

		switch (dictstate) {
		case 0:
			if (ben_current_char(ctx) == '}') {
				ctx->off++;
				goto exit;
			}
			key = decode_printed(ctx);
			if (key == NULL)
				goto nullpath;
			dictstate = 1;
			break;
		case 1:
			if (ben_current_char(ctx) != ':')
				goto invalidpath;
			ctx->off++;
			dictstate = 2;
			break;
		case 2:
			value = decode_printed(ctx);
			if (value == NULL)
				goto nullpath;
			assert(key != NULL);
			if (ben_dict_set(d, key, value)) {
				ben_free(key);
				ben_free(value);
				ben_free(d);
				return ben_oom_ptr(ctx);
			}
			key = NULL;
			value = NULL;
			dictstate = 3;
			break;
		case 3:
			if (ben_current_char(ctx) == '}') {
				ctx->off++;
				goto exit;
			}
			if (ben_current_char(ctx) != ',')
				goto invalidpath;
			ctx->off++;
			dictstate = 0;
			break;
		default:
			die("Invalid dictstate: %d\n", dictstate);
		}
	}

exit:
	return d;

invalidpath:
	ben_free(key);
	ben_free(value);
	ben_free(d);
	return ben_invalid_ptr(ctx);

nullpath:
	ben_free(key);
	ben_free(value);
	ben_free(d);
	return NULL;
}

static struct bencode *decode_printed_int(struct ben_decode_ctx *ctx)
{
	long long ll;
	char buf[LONGLONGSIZE];
	char *end;
	size_t pos = 0;
	struct bencode *b;
	int gotzero = 0;
	int base = 10;
	int neg = 0;

	if (ben_current_char(ctx) == '-') {
		neg = 1;
		ctx->off++;
	}
	if (ctx->off == ctx->len)
		return ben_insufficient_ptr(ctx);

	if (ben_current_char(ctx) == '0') {
		buf[pos] = '0';
		pos++;
		ctx->off++;
		gotzero = 1;
	}

	if (gotzero) {
		if (ctx->off == ctx->len) {
			ll = 0;
			goto returnwithval;
		}
		if (ben_current_char(ctx) == 'x') {
			pos = 0;
			base = 16;
			ctx->off++;
			if (ctx->off == ctx->len)
				return ben_insufficient_ptr(ctx);
		} else if (isdigit(ben_current_char(ctx))) {
			base = 8;
		}
	} else {
		if (ctx->off == ctx->len)
			return ben_insufficient_ptr(ctx);
	}

	while (ctx->off < ctx->len && pos < sizeof buf) {
		char c = ben_current_char(ctx);
		if (base == 16) {
			if (!isxdigit(c))
				break;
		} else {
			if (!isdigit(c))
				break;
		}
		buf[pos] = c;
		pos++;
		ctx->off++;
	}
	if (pos == 0 || pos == sizeof buf)
		return ben_invalid_ptr(ctx);
	buf[pos] = 0;
	ll = strtoll(buf, &end, base);
	if (*end != 0)
		return ben_invalid_ptr(ctx);

returnwithval:
	if (neg)
		ll = -ll;
	b = ben_int(ll);
	if (b == NULL)
		return ben_oom_ptr(ctx);
	return b;
}

static struct bencode *decode_printed_list(struct ben_decode_ctx *ctx)
{
	struct bencode *l = ben_list();
	int requirecomma = 0;
	struct bencode *b;

	if (l == NULL)
		return ben_oom_ptr(ctx);

	ctx->off++;

	while (ctx->off < ctx->len) {
		if (seek_char(ctx)) {
			ben_free(l);
			return NULL;
		}
		if (ben_current_char(ctx) == ']') {
			ctx->off++;
			break;
		}
		if (requirecomma) {
			if (ben_current_char(ctx) != ',') {
				ben_free(l);
				return ben_invalid_ptr(ctx);
			}
			ctx->off++;
			requirecomma = 0;
		} else {
			b = decode_printed(ctx);
			if (b == NULL) {
				ben_free(l);
				return NULL;
			}
			if (ben_list_append(l, b)) {
				ben_free(l);
				return ben_oom_ptr(ctx);
			}
			requirecomma = 1;
		}
	}
	return l;
}

static struct bencode *decode_printed_str(struct ben_decode_ctx *ctx)
{
	size_t pos;
	char *s = NULL;
	size_t len = 0;
	char initial = ben_current_char(ctx);
	struct bencode *b;

	ctx->off++;
	pos = ctx->off;
	while (pos < ctx->len) {
		char c = ctx->data[pos];
		if (!isprint(c))
			return ben_invalid_ptr(ctx);
		if (c == initial)
			break;
		len++;
		pos++;
		if (c != '\\')
			continue; /* Normal printable char, e.g. 'a' */
		/* Handle '\\' */
		if (pos == ctx->len)
			return ben_insufficient_ptr(ctx);

		c = ctx->data[pos];
		pos++;
		if (c == 'x') {
			/* hexadecimal value: \xHH */
			pos += 2;
		}
	}
	if (pos >= ctx->len)
		return ben_insufficient_ptr(ctx);

	s = malloc(len + 1);
	if (s == NULL)
		return ben_oom_ptr(ctx);

	pos = 0;
	while (ctx->off < ctx->len) {
		char c = ben_current_char(ctx);
		assert(isprint(c));
		if (c == initial)
			break;
		assert(pos < len);
		ctx->off++;
		if (c != '\\') {
			s[pos] = c;
			pos++;
			continue; /* Normal printable char, e.g. 'a' */
		}
		/* Handle '\\' */

		/*
		 * Note, we do assert because we have already verified in the
		 * previous loop that there is sufficient data.
		 */
		assert(ctx->off != ctx->len);
		c = ben_current_char(ctx);
		ctx->off++;
		if (c == 'x') {
			/* hexadecimal value: \xHH */
			char *end;
			unsigned long x;
			char buf[3];
			assert((ctx->off + 1) < ctx->len);
			buf[0] = ctx->data[ctx->off + 0];
			buf[1] = ctx->data[ctx->off + 1];
			buf[2] = 0;
			ctx->off += 2;
			x = strtoul(buf, &end, 16);
			if (*end != 0)
				goto invalid;
			assert(x < 256);
			c = (char) x;
		}
		s[pos] = c;
		pos++;
	}
	assert(pos == len);
	if (ctx->off >= ctx->len)
		return ben_insufficient_ptr(ctx);
	ctx->off++;

	s[pos] = 0; /* the area must always be zero terminated! */

	b = internal_blob(s, len);
	if (b == NULL) {
		free(s);
		return ben_oom_ptr(ctx);
	}
	return b;

invalid:
	free(s);
	return ben_invalid_ptr(ctx);
}

static struct bencode *decode_printed(struct ben_decode_ctx *ctx)
{
	struct bencode *b;

	ctx->level++;
	if (ctx->level > 256)
		return ben_invalid_ptr(ctx);

	if (seek_char(ctx))
		return NULL;

	switch (ben_current_char(ctx)) {
	case '\'':
	case '"':
		b = decode_printed_str(ctx);
		break;
	case '-':
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		b = decode_printed_int(ctx);
		break;
	case 'F':
	case 'T':
		b = decode_printed_bool(ctx);
		break;
	case '[':
		b = decode_printed_list(ctx);
		break;
	case '{':
		b = decode_printed_dict(ctx);
		break;
	default:
		return ben_invalid_ptr(ctx);
	}
	ctx->level--;
	return b;
}

struct bencode *ben_decode_printed(const void *data, size_t len)
{
	struct ben_decode_ctx ctx = {.data = data, .len = len};
	return decode_printed(&ctx);
}

struct bencode *ben_decode_printed2(const void *data, size_t len, size_t *off, struct bencode_error *error)
{
	struct ben_decode_ctx ctx = {.data = data, .len = len, .off = *off};
	struct bencode *b = decode_printed(&ctx);
	*off = ctx.off;
	if (error != NULL) {
		assert((b != NULL) ^ (ctx.error != 0));
		error->error = ctx.error;
		if (b != NULL) {
			error->off = 0;
			error->line = 0;
		} else {
			error->off = ctx.off;
			error->line = ctx.line;
		}
	}
	return b;
}

static void free_dict(struct bencode_dict *d)
{
	size_t pos;
	for (pos = 0; pos < d->n; pos++) {
		ben_free(d->nodes[pos].key);
		ben_free(d->nodes[pos].value);
		d->nodes[pos].key = NULL;
		d->nodes[pos].value = NULL;
	}
	free(d->buckets);
	free(d->nodes);
}

static void free_list(struct bencode_list *list)
{
	size_t pos;
	for (pos = 0; pos < list->n; pos++) {
		ben_free(list->values[pos]);
		list->values[pos] = NULL;
	}
	free(list->values);
}

int ben_put_char(struct ben_encode_ctx *ctx, char c)
{
	if (ctx->pos >= ctx->size)
		return -1;
	ctx->data[ctx->pos] = c;
	ctx->pos += 1;
	return 0;
}

int ben_put_buffer(struct ben_encode_ctx *ctx, const void *buf, size_t len)
{
	if ((ctx->pos + len) > ctx->size)
		return -1;
	memcpy(ctx->data + ctx->pos, buf, len);
	ctx->pos += len;
	return 0;
}

static int puthexchar(struct ben_encode_ctx *ctx, unsigned char hex)
{
	char buf[5];
	int len = snprintf(buf, sizeof buf, "\\x%.2x", hex);
	assert(len == 4);
	return ben_put_buffer(ctx, buf, len);
}

static int putlonglong(struct ben_encode_ctx *ctx, long long ll)
{
	char buf[LONGLONGSIZE];
	int len = snprintf(buf, sizeof buf, "%lld", ll);
	assert(len > 0);
	return ben_put_buffer(ctx, buf, len);
}

static int putunsignedlonglong(struct ben_encode_ctx *ctx, unsigned long long llu)
{
	char buf[LONGLONGSIZE];
	int len = snprintf(buf, sizeof buf, "%llu", llu);
	assert(len > 0);
	return ben_put_buffer(ctx, buf, len);
}

static int putstr(struct ben_encode_ctx *ctx, char *s)
{
	return ben_put_buffer(ctx, s, strlen(s));
}

static int print(struct ben_encode_ctx *ctx, const struct bencode *b)
{
	const struct bencode_bool *boolean;
	const struct bencode_dict *dict;
	const struct bencode_int *integer;
	const struct bencode_list *list;
	const struct bencode_str *s;
	size_t i;
	int len;
	struct bencode_keyvalue *pairs;

	switch (b->type) {
	case BENCODE_BOOL:
		boolean = ben_bool_const_cast(b);
		len = boolean->b ? 4 : 5;
		if (ctx->pos + len > ctx->size)
			return -1;
		memcpy(ctx->data + ctx->pos, (len == 4) ? "True" : "False", len);
		ctx->pos += len;
		return 0;

	case BENCODE_DICT:
		if (ben_put_char(ctx, '{'))
			return -1;

		dict = ben_dict_const_cast(b);

		pairs = malloc(dict->n * sizeof(pairs[0]));
		if (pairs == NULL) {
			warn("No memory for dict serialization\n");
			return -1;
		}
		for (i = 0; i < dict->n; i++) {
			pairs[i].key = dict->nodes[i].key;
			pairs[i].value = dict->nodes[i].value;
		}
		qsort(pairs, dict->n, sizeof(pairs[0]), ben_cmp_qsort);

		for (i = 0; i < dict->n; i++) {
			if (print(ctx, pairs[i].key))
				break;
			if (putstr(ctx, ": "))
				break;
			if (print(ctx, pairs[i].value))
				break;
			if (i < (dict->n - 1)) {
				if (putstr(ctx, ", "))
					break;
			}
		}
		free(pairs);
		pairs = NULL;
		if (i < dict->n)
			return -1;

		return ben_put_char(ctx, '}');

	case BENCODE_INT:
		integer = ben_int_const_cast(b);

		if (putlonglong(ctx, integer->ll))
			return -1;

		return 0;

	case BENCODE_LIST:
		if (ben_put_char(ctx, '['))
			return -1;
		list = ben_list_const_cast(b);
		for (i = 0; i < list->n; i++) {
			if (print(ctx, list->values[i]))
				return -1;
			if (i < (list->n - 1) && putstr(ctx, ", "))
				return -1;
		}
		return ben_put_char(ctx, ']');

	case BENCODE_STR:
		s = ben_str_const_cast(b);
		if (ben_put_char(ctx, '\''))
			return -1;
		for (i = 0; i < s->len; i++) {
			if (!isprint(s->s[i])) {
				if (puthexchar(ctx, s->s[i]))
					return -1;
				continue;
			}

			switch (s->s[i]) {
			case '\'':
			case '\\':
				/* Need escape character */
				if (ben_put_char(ctx, '\\'))
					return -1;
			default:
				if (ben_put_char(ctx, s->s[i]))
					return -1;
				break;
			}
		}
		return ben_put_char(ctx, '\'');
	default:
		die("serialization type %d not implemented\n", b->type);
	}
}

static size_t get_printed_size(const struct bencode *b)
{
	size_t pos;
	const struct bencode_bool *boolean;
	const struct bencode_dict *d;
	const struct bencode_int *i;
	const struct bencode_list *l;
	const struct bencode_str *s;
	size_t size = 0;
	char buf[1];

	switch (b->type) {
	case BENCODE_BOOL:
		boolean = ben_bool_const_cast(b);
		return boolean->b ? 4 : 5; /* "True" and "False" */
	case BENCODE_DICT:
		size += 1; /* "{" */
		d = ben_dict_const_cast(b);
		for (pos = 0; pos < d->n; pos++) {
			size += get_printed_size(d->nodes[pos].key);
			size += 2; /* ": " */
			size += get_printed_size(d->nodes[pos].value);
			if (pos < (d->n - 1))
				size += 2; /* ", " */
		}
		size += 1; /* "}" */
		return size;
	case BENCODE_INT:
		i = ben_int_const_cast(b);
		return snprintf(buf, 0, "%lld", i->ll);
	case BENCODE_LIST:
		size += 1; /* "[" */
		l = ben_list_const_cast(b);
		for (pos = 0; pos < l->n; pos++) {
			size += get_printed_size(l->values[pos]);
			if (pos < (l->n - 1))
				size += 2; /* ", " */
		}
		size += 1; /* "]" */
		return size;
	case BENCODE_STR:
		s = ben_str_const_cast(b);
		size += 1; /* ' */
		for (pos = 0; pos < s->len; pos++) {
			if (!isprint(s->s[pos])) {
				size += 4; /* "\xDD" */
				continue;
			}
			switch (s->s[pos]) {
			case '\'':
			case '\\':
				size += 2; /* escaped characters */
				break;
			default:
				size += 1;
				break;
			}
		}
		size += 1; /* ' */
		return size;
	default:
		die("Unknown type: %c\n", b->type);
	}
}

static int serialize(struct ben_encode_ctx *ctx, const struct bencode *b)
{
	const struct bencode_dict *dict;
	const struct bencode_int *integer;
	const struct bencode_list *list;
	const struct bencode_str *s;
	const struct bencode_user *u;
	size_t i;
	struct bencode_keyvalue *pairs;

	switch (b->type) {
	case BENCODE_BOOL:
		if ((ctx->pos + 2) > ctx->size)
			return -1;
		ctx->data[ctx->pos] = 'b';
		ctx->data[ctx->pos + 1] = ben_bool_const_cast(b)->b ? '1' : '0';
		ctx->pos += 2;
		return 0;

	case BENCODE_DICT:
		if (ben_put_char(ctx, 'd'))
			return -1;

		dict = ben_dict_const_cast(b);

		pairs = malloc(dict->n * sizeof(pairs[0]));
		if (pairs == NULL) {
			warn("No memory for dict serialization\n");
			return -1;
		}
		for (i = 0; i < dict->n; i++) {
			pairs[i].key = dict->nodes[i].key;
			pairs[i].value = dict->nodes[i].value;
		}
		qsort(pairs, dict->n, sizeof(pairs[0]), ben_cmp_qsort);

		for (i = 0; i < dict->n; i++) {
			if (serialize(ctx, pairs[i].key))
				break;
			if (serialize(ctx, pairs[i].value))
				break;
		}
		free(pairs);
		pairs = NULL;
		if (i < dict->n)
			return -1;

		return ben_put_char(ctx, 'e');

	case BENCODE_INT:
		if (ben_put_char(ctx, 'i'))
			return -1;
		integer = ben_int_const_cast(b);
		if (putlonglong(ctx, integer->ll))
			return -1;
		return ben_put_char(ctx, 'e');

	case BENCODE_LIST:
		if (ben_put_char(ctx, 'l'))
			return -1;

		list = ben_list_const_cast(b);
		for (i = 0; i < list->n; i++) {
			if (serialize(ctx, list->values[i]))
				return -1;
		}

		return ben_put_char(ctx, 'e');

	case BENCODE_STR:
		s = ben_str_const_cast(b);
		if (putunsignedlonglong(ctx, ((long long) s->len)))
			return -1;
		if (ben_put_char(ctx, ':'))
			return -1;
		if ((ctx->pos + s->len) > ctx->size)
			return -1;
		memcpy(ctx->data + ctx->pos, s->s, s->len);
		ctx->pos += s->len;
		return 0;

	case BENCODE_USER:
		u = ben_user_const_cast(b);
		return u->info->encode(ctx, b);

	default:
		die("serialization type %d not implemented\n", b->type);
	}
}

static size_t get_size(const struct bencode *b)
{
	size_t pos;
	const struct bencode_dict *d;
	const struct bencode_int *i;
	const struct bencode_list *l;
	const struct bencode_str *s;
	const struct bencode_user *u;
	size_t size = 0;
	char buf[1];

	switch (b->type) {
	case BENCODE_BOOL:
		return 2;
	case BENCODE_DICT:
		d = ben_dict_const_cast(b);
		for (pos = 0; pos < d->n; pos++) {
			size += get_size(d->nodes[pos].key);
			size += get_size(d->nodes[pos].value);
		}
		return size + 2;
	case BENCODE_INT:
		i = ben_int_const_cast(b);
		return 2 + snprintf(buf, 0, "%lld", i->ll);
	case BENCODE_LIST:
		l = ben_list_const_cast(b);
		for (pos = 0; pos < l->n; pos++)
			size += get_size(l->values[pos]);
		return size + 2;
	case BENCODE_STR:
		s = ben_str_const_cast(b);
		return snprintf(buf, 0, "%zu", s->len) + 1 + s->len;
	case BENCODE_USER:
		u = ben_user_const_cast(b);
		return u->info->get_size(b);
	default:
		die("Unknown type: %c\n", b->type);
	}
}

size_t ben_encoded_size(const struct bencode *b)
{
	return get_size(b);
}

void *ben_encode(size_t *len, const struct bencode *b)
{
	size_t size = get_size(b);
	void *data = malloc(size);
	struct ben_encode_ctx ctx = {.data = data, .size = size, .pos = 0};
	if (data == NULL) {
		warn("No memory to encode\n");
		return NULL;
	}
	ctx.pos = 0;
	if (serialize(&ctx, b)) {
		free(ctx.data);
		return NULL;
	}
	assert(ctx.pos == size);
	*len = ctx.pos;
	return data;
}

size_t ben_encode2(char *data, size_t maxlen, const struct bencode *b)
{
	struct ben_encode_ctx ctx = {.data = data, .size = maxlen, .pos = 0};
	if (serialize(&ctx, b))
		return -1;
	return ctx.pos;
}

void ben_free(struct bencode *b)
{
	struct bencode_str *s;
	struct bencode_user *u;
	size_t size;
	if (b == NULL)
		return;
	switch (b->type) {
	case BENCODE_BOOL:
		break;
	case BENCODE_DICT:
		free_dict(ben_dict_cast(b));
		break;
	case BENCODE_INT:
		break;
	case BENCODE_LIST:
		free_list(ben_list_cast(b));
		break;
	case BENCODE_STR:
		s = ben_str_cast(b);
		free(s->s);
		break;
	case BENCODE_USER:
		u = ben_user_cast(b);
		if (u->info->free)
			u->info->free(b);
		break;
	default:
		die("invalid type: %d\n", b->type);
	}

	if (b->type == BENCODE_USER)
		size = ((struct bencode_user *) b)->info->size;
	else
		size = type_size(b->type);
	memset(b, -1, size); /* data poison */
	free(b);
}

struct bencode *ben_blob(const void *data, size_t len)
{
	struct bencode_str *b = alloc(BENCODE_STR);
	if (b == NULL)
		return NULL;
	/* Allocate one extra byte for zero termination for convenient use */
	b->s = malloc(len + 1);
	if (b->s == NULL) {
		free(b);
		return NULL;
	}
	memcpy(b->s, data, len);
	b->len = len;
	b->s[len] = 0;
	return (struct bencode *) b;
}

struct bencode *ben_bool(int boolean)
{
	struct bencode_bool *b = alloc(BENCODE_BOOL);
	if (b == NULL)
		return NULL;
	b->b = boolean ? 1 : 0;
	return (struct bencode *) b;
}

struct bencode *ben_dict(void)
{
	return alloc(BENCODE_DICT);
}

struct bencode *ben_dict_get(const struct bencode *dict, const struct bencode *key)
{
	const struct bencode_dict *d = ben_dict_const_cast(dict);
	long long hash = ben_hash(key);
	size_t pos = hash_bucket_head(hash, d);
	while (pos != -1) {
		assert(pos < d->n);
		if (d->nodes[pos].hash == hash &&
		    ben_cmp(d->nodes[pos].key, key) == 0)
			return d->nodes[pos].value;
		pos = d->nodes[pos].next;
	}
	return NULL;
}

/*
 * Note, we do not re-allocate memory, so one may not call ben_free for these
 * instances. These are only used to optimize speed.
 */
static void inplace_ben_str(struct bencode_str *b, const char *s, size_t len)
{
	b->type = BENCODE_STR;
	b->len = len;
	b->s = (char *) s;
}
static void inplace_ben_int(struct bencode_int *i, long long ll)
{
	i->type = BENCODE_INT;
	i->ll = ll;
}

struct bencode *ben_dict_get_by_str(const struct bencode *dict, const char *key)
{
	struct bencode_str s;
	inplace_ben_str(&s, key, strlen(key));
	return ben_dict_get(dict, (struct bencode *) &s);
}

struct bencode *ben_dict_get_by_int(const struct bencode *dict, long long key)
{
	struct bencode_int i;
	inplace_ben_int(&i, key);
	return ben_dict_get(dict, (struct bencode *) &i);
}

static size_t dict_find_pos(struct bencode_dict *d,
			    const struct bencode *key, long long hash)
{
	size_t pos = hash_bucket_head(hash, d);
	while (pos != -1) {
		assert(pos < d->n);
		if (d->nodes[pos].hash == hash &&
		    ben_cmp(d->nodes[pos].key, key) == 0)
			break;
		pos = d->nodes[pos].next;
	}
	return pos;
}

static void dict_unlink(struct bencode_dict *d, size_t bucket, size_t unlinkpos)
{
	size_t pos = d->buckets[bucket];
	size_t next;
	size_t nextnext;

	assert(unlinkpos < d->n);

	if (pos == unlinkpos) {
		next = d->nodes[unlinkpos].next;
		assert(next < d->n || next == -1);
		d->buckets[bucket] = next;
		return;
	}
	while (pos != -1) {
		assert(pos < d->n);
		next = d->nodes[pos].next;
		if (next == unlinkpos) {
			nextnext = d->nodes[next].next;
			assert(nextnext < d->n || nextnext == -1);
			d->nodes[pos].next = nextnext;
			return;
		}
		pos = next;
	}
	die("Key should have been found. Can not unlink position %zu.\n", unlinkpos);
}

/* Remove node from the linked list, if found */
static struct bencode *dict_pop(struct bencode_dict *d, 
				const struct bencode *key, long long hash)
{
	struct bencode *value;
	size_t removebucket = hash_bucket(hash, d);
	size_t tailpos = d->n - 1;
	size_t tailhash = d->nodes[tailpos].hash;
	size_t tailbucket = hash_bucket(tailhash, d);
	size_t removepos;

	removepos = dict_find_pos(d, key, hash);
	if (removepos == -1)
		return NULL;
	key = NULL; /* avoid using the pointer again, it may not be valid */

	/*
	 * WARNING: complicated code follows.
	 *
	 * First, unlink the node to be removed and the tail node.
	 * We will actually later swap the positions of removed node and
	 * tail node inside the d->nodes array. We want to preserve
	 * d->nodes array in a state where positions from 0 to (d->n - 1)
	 * are always occupied with a valid node. This is done to make
	 * dictionary walk fast by simply walking positions 0 to (d->n - 1)
	 * in a for loop.
	 */
	dict_unlink(d, removebucket, removepos);
	if (removepos != tailpos)
		dict_unlink(d, tailbucket, tailpos);

	/* Then read the removed node and free its key */
	value = d->nodes[removepos].value;
	ben_free(d->nodes[removepos].key);

	/* Then re-insert the unliked tail node in the place of removed node */
	d->nodes[removepos] = d->nodes[tailpos];
	memset(&d->nodes[tailpos], 0, sizeof d->nodes[tailpos]); /* poison */
	d->nodes[tailpos].next = ((size_t) -1) / 2;

	/*
	 * Then re-link the tail node to its bucket, unless the tail node
	 * was the one to be removed.
	 */
	if (removepos != tailpos) {
		d->nodes[removepos].next = d->buckets[tailbucket];
		d->buckets[tailbucket] = removepos;
	}

	d->n -= 1;
	return value;
}

struct bencode *ben_dict_pop(struct bencode *dict, const struct bencode *key)
{
	struct bencode_dict *d = ben_dict_cast(dict);
	return dict_pop(d, key, ben_hash(key));
}

struct bencode *ben_dict_pop_by_str(struct bencode *dict, const char *key)
{
	struct bencode_str s;
	inplace_ben_str(&s, key, strlen(key));
	return ben_dict_pop(dict, (struct bencode *) &s);
}

struct bencode *ben_dict_pop_by_int(struct bencode *dict, long long key)
{
	struct bencode_int i;
	inplace_ben_int(&i, key);
	return ben_dict_pop(dict, (struct bencode *) &i);
}

/* This can be used from the ben_dict_for_each() iterator */
struct bencode *ben_dict_pop_current(struct bencode *dict, size_t *pos)
{
	struct bencode_dict *d = ben_dict_cast(dict);
	struct bencode *value = ben_dict_pop(dict, d->nodes[*pos].key);
	(*pos)--;
	return value;
}

int ben_dict_set(struct bencode *dict, struct bencode *key, struct bencode *value)
{
	struct bencode_dict *d = ben_dict_cast(dict);
	long long hash = ben_hash(key);
	size_t bucket;
	size_t pos;

	assert(value != NULL);

	pos = hash_bucket_head(hash, d);
	for (; pos != -1; pos = d->nodes[pos].next) {
		assert(pos < d->n);
		if (d->nodes[pos].hash != hash || ben_cmp(d->nodes[pos].key, key) != 0)
			continue;
		ben_free(d->nodes[pos].key);
		ben_free(d->nodes[pos].value);
		d->nodes[pos].key = key;
		d->nodes[pos].value = value;
		/* 'hash' and 'next' members stay the same */
		return 0;
	}

	assert(d->n <= d->alloc);
	if (d->n == d->alloc && resize_dict(d, -1))
		return -1;

	bucket = hash_bucket(hash, d);
	pos = d->n;
	d->nodes[pos] = (struct bencode_dict_node) {.hash = hash,
						    .key = key,
						    .value = value,
						    .next = d->buckets[bucket]};
	d->n++;
	d->buckets[bucket] = pos;
	return 0;
}

int ben_dict_set_by_str(struct bencode *dict, const char *key, struct bencode *value)
{
	struct bencode *bkey = ben_str(key);
	if (bkey == NULL)
		return -1;
	if (ben_dict_set(dict, bkey, value)) {
		ben_free(bkey);
		return -1;
	}
	return 0;
}

int ben_dict_set_str_by_str(struct bencode *dict, const char *key, const char *value)
{
	struct bencode *bkey = ben_str(key);
	struct bencode *bvalue = ben_str(value);
	if (bkey == NULL || bvalue == NULL) {
		ben_free(bkey);
		ben_free(bvalue);
		return -1;
	}
	if (ben_dict_set(dict, bkey, bvalue)) {
		ben_free(bkey);
		ben_free(bvalue);
		return -1;
	}
	return 0;
}

struct bencode *ben_int(long long ll)
{
	struct bencode_int *b = alloc(BENCODE_INT);
	if (b == NULL)
		return NULL;
	b->ll = ll;
	return (struct bencode *) b;
}

struct bencode *ben_list(void)
{
	return alloc(BENCODE_LIST);
}

int ben_list_append(struct bencode *list, struct bencode *b)
{
	struct bencode_list *l = ben_list_cast(list);
	/* NULL pointer de-reference if the cast fails */
	assert(l->n <= l->alloc);
	if (l->n == l->alloc && resize_list(l, -1))
		return -1;
	assert(b != NULL);
	l->values[l->n] = b;
	l->n += 1;
	return 0;
}

int ben_list_append_str(struct bencode *list, const char *s)
{
	struct bencode *bs = ben_str(s);
	if (bs == NULL)
		return -1;
	return ben_list_append(list, bs);
}

int ben_list_append_int(struct bencode *list, long long ll)
{
	struct bencode *bll = ben_int(ll);
	if (bll == NULL)
		return -1;
	return ben_list_append(list, bll);
}

struct bencode *ben_list_pop(struct bencode *list, size_t pos)
{
	struct bencode_list *l = ben_list_cast(list);
	struct bencode *value;

	assert(pos < l->n);

	value = ben_list_get(list, pos);

	for (; (pos + 1) < l->n; pos++)
		l->values[pos] = l->values[pos + 1];

	l->values[l->n - 1] = NULL;
	l->n--;
	return value;
}

void ben_list_set(struct bencode *list, size_t i, struct bencode *b)
{
	struct bencode_list *l = ben_list_cast(list);
	if (i >= l->n)
		die("ben_list_set() out of bounds: %zu\n", i);

	ben_free(l->values[i]);
	assert(b != NULL);
	l->values[i] = b;
}

char *ben_print(const struct bencode *b)
{
	size_t size = get_printed_size(b);
	char *data = malloc(size + 1);
	struct ben_encode_ctx ctx = {.data = data, .size = size, .pos = 0};
	if (data == NULL) {
		warn("No memory to print\n");
		return NULL;
	}
	if (print(&ctx, b)) {
		free(data);
		return NULL;
	}
	assert(ctx.pos == size);
	data[ctx.pos] = 0;
	return data;
}

struct bencode *ben_str(const char *s)
{
	return ben_blob(s, strlen(s));
}

const char *ben_strerror(int error)
{
	switch (error) {
	case BEN_OK:
		return "OK (no error)";
	case BEN_INVALID:
		return "Invalid data";
	case BEN_INSUFFICIENT:
		return "Insufficient amount of data (need more data)";
	case BEN_NO_MEMORY:
		return "Out of memory";
	default:
		fprintf(stderr, "Unknown error code: %d\n", error);
		return NULL;
	}
}
