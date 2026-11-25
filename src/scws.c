#include "scws.h"
#include "utf8.h"
#include "xdb.h"
#include "rules.h"
#include <stdlib.h>
#include <string.h>

typedef scws_unit unit;
typedef struct {
	uint64_t cost;
	size_t to, tokens;
	char pos[3];
	scws_origin_t origin;
	int emit;
} path;
struct scws {
	scws_dict_t **dicts;
	size_t dict_count, dict_capacity;
	unsigned flags;
	uint32_t unknown_cost;
	scws_status_t error;
};
struct scws_result {
	scws_token_t *tokens;
	size_t count, capacity, cursor;
};
static scws_status_t set_error(scws_t ctx, scws_status_t error)
{
	if (ctx) ctx->error = error;
	return error;
}

static scws_status_t reserve(void *array, size_t *capacity, size_t need, size_t width, void **out)
{
	size_t next;
	void *grown;
	*out = array;
	if (need <= *capacity) return SCWS_OK;
	if (need > SIZE_MAX / width) return SCWS_ELIMIT;
	next = *capacity ? *capacity : 16;
	while (next < need) {
		if (next > SIZE_MAX / 2) { next = need; break; }
		next *= 2;
	}
	if (next > SIZE_MAX / width) next = need;
	grown = realloc(array, next * width);
	if (!grown) return SCWS_ENOMEM;
	*out = grown; *capacity = next;
	return SCWS_OK;
}
static void clear_dicts(scws_t ctx)
{
	for (size_t i = 0; i < ctx->dict_count; i++) scws_dict_close(ctx->dicts[i]);
	ctx->dict_count = 0;
}
static scws_status_t load_dict(scws_t ctx, const char *filename, scws_dict_flags_t flags, int replace)
{
	scws_dict_t *dict = NULL;
	scws_status_t status;
	void *grown;
	if (!ctx) return SCWS_EINVAL;
	if (!replace && ctx->dict_count == SIZE_MAX) return SCWS_ELIMIT;
	status = scws_dict_open_ex(filename, flags, &dict);
	if (status) return status;
	status = reserve(ctx->dicts, &ctx->dict_capacity, replace ? 1 : ctx->dict_count + 1,
		sizeof(*ctx->dicts), &grown);
	if (status) { scws_dict_close(dict); return status; }
	ctx->dicts = grown;
	if (replace) clear_dicts(ctx);
	ctx->dicts[ctx->dict_count++] = dict;
	return SCWS_OK;
}
scws_status_t scws_set_dict(scws_t ctx, const char *filename, scws_dict_flags_t flags)
{
	return set_error(ctx, load_dict(ctx, filename, flags, 1));
}
scws_status_t scws_add_dict(scws_t ctx, const char *filename, scws_dict_flags_t flags)
{
	return set_error(ctx, load_dict(ctx, filename, flags, 0));
}
scws_t scws_new(void)
{
	scws_t ctx = calloc(1, sizeof(*ctx));
	if (ctx) ctx->unknown_cost = 18432;
	return ctx;
}
void scws_free(scws_t ctx)
{
	if (!ctx) return;
	clear_dicts(ctx);
	free(ctx->dicts); free(ctx);
}
scws_status_t scws_set_options(scws_t ctx, unsigned flags)
{
	if (!ctx || flags & ~(SCWS_KEEP_SYMBOLS | SCWS_FALLBACK_BIGRAM)) return set_error(ctx, SCWS_EINVAL);
	ctx->flags = flags;
	return set_error(ctx, SCWS_OK);
}
scws_status_t scws_set_unknown_cost(scws_t ctx, uint32_t cost)
{
	if (!ctx || !cost) return set_error(ctx, SCWS_EINVAL);
	ctx->unknown_cost = cost;
	return set_error(ctx, SCWS_OK);
}

static int alnum(uint32_t ch) { return scws_alpha(ch) || scws_digit(ch); }
static int connector(uint32_t ch)
{
	return ch == '-' || ch == '_' || ch == '.' || ch == '/' || ch == '&' || ch == '\'';
}
static int boundary(uint32_t ch)
{
	/* Middle dot is a separator by itself, but may belong to a dictionary word. */
	return scws_space(ch) || (scws_symbol(ch) && !connector(ch) && ch != '+' && ch != '#' && ch != 0xb7);
}
static void atomize(unit *units, size_t count)
{
	for (size_t i = 0; i < count;) {
		size_t end = i;
		int letters = 0;
		if (!alnum(units[i].ch)) { i++; continue; }
		while (end < count) {
			uint32_t ch = units[end].ch;
			if (alnum(ch)) { letters |= scws_alpha(ch); end++; }
			else if (connector(ch) && end + 1 < count && alnum(units[end - 1].ch) && alnum(units[end + 1].ch)) end++;
			else if (letters && (ch == '+' || ch == '#')) {
				while (end < count && (units[end].ch == '+' || units[end].ch == '#')) end++;
				break;
			} else break;
		}
		letters = 0;
		for (size_t j = end; j-- > i;) {
			letters |= scws_alpha(units[j].ch);
			if (alnum(units[j].ch)) {
				units[j].atom_end = end;
				strcpy(units[j].atom_pos, letters ? "en" : "m");
			}
		}
		i = end;
	}
}
static int lookup(scws_t ctx, const unsigned char *key, size_t length, scws_word *out)
{
	scws_word word;
	uint32_t prefix = 0;
	int found = 0;
	memset(out, 0, sizeof(*out));
	for (size_t i = 0; i < ctx->dict_count; i++) {
		if (!scws_dict_lookup(ctx->dicts[i], key, length, &word)) continue;
		prefix |= word.flags & SCWS_WORD_PREFIX;
		if (word.flags & SCWS_WORD_SEARCH) { *out = word; found = 1; }
	}
	out->flags = (out->flags & ~SCWS_WORD_PREFIX) | prefix;
	return found;
}
static void consider(path *paths, size_t from, size_t to, uint32_t cost, int emit,
	const char *pos, scws_origin_t origin)
{
	path *current = &paths[from];
	uint64_t score = paths[to].cost + cost;
	size_t tokens = paths[to].tokens + (size_t) emit;
	if (score < current->cost || (score == current->cost && (tokens < current->tokens
		|| (tokens == current->tokens && (to > current->to || (to == current->to && origin < current->origin)))))) {
		current->cost = score; current->to = to; current->tokens = tokens;
		current->emit = emit; current->origin = origin;
		strcpy(current->pos, pos);
	}
}
static scws_status_t append(scws_result_t result, const unit *units, size_t from, size_t to,
	const char *pos, scws_kind_t kind, scws_origin_t origin)
{
	scws_status_t status;
	scws_token_t *token;
	void *grown;
	if (result->count == SIZE_MAX) return SCWS_ELIMIT;
	status = reserve(result->tokens, &result->capacity, result->count + 1, sizeof(*result->tokens), &grown);
	if (status) return status;
	result->tokens = grown;
	token = &result->tokens[result->count++];
	token->offset = units[from].offset;
	token->length = units[to].offset - units[from].offset;
	token->char_offset = from; token->char_length = to - from;
	strcpy(token->pos, pos);
	token->kind = kind; token->origin = origin;
	return SCWS_OK;
}
static int token_compare(const void *left, const void *right)
{
	const scws_token_t *a = left, *b = right;
	if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
	if (a->length != b->length) return a->length > b->length ? -1 : 1;
	if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
	return a->origin < b->origin ? -1 : a->origin > b->origin;
}

scws_result_t scws_segment(scws_t ctx, const char *input, size_t length, scws_mode_t mode)
{
	const unsigned char *text = (const unsigned char *) input;
	unit *units = NULL;
	path *paths = NULL;
	unsigned char *unknown = NULL;
	scws_result_t result = NULL;
	size_t count = 0, offset = 0;
	scws_status_t status = SCWS_OK;
	if (!ctx) return NULL;
	if ((!input && length) || (mode != SCWS_STANDARD && mode != SCWS_SEARCH)) {
		set_error(ctx, SCWS_EINVAL); return NULL;
	}
	/* Counts also bound cost accumulation and all size multiplications. */
	if (length > SIZE_MAX / sizeof(unit) - 1 || length > SIZE_MAX / sizeof(path) - 1
		|| length > UINT64_MAX / UINT32_MAX - 1) {
		set_error(ctx, SCWS_ELIMIT); return NULL;
	}
	while (offset < length) {
		uint32_t ch;
		size_t size = scws_utf8_decode(text + offset, length - offset, &ch);
		if (!size) { status = SCWS_EUTF8; goto done; }
		offset += size; count++;
	}
	result = calloc(1, sizeof(*result));
	if (!result) { status = SCWS_ENOMEM; goto done; }
	if (!length) goto done;
	units = calloc(count + 1, sizeof(*units));
	paths = calloc(count + 1, sizeof(*paths));
	unknown = calloc(count, 1);
	if (!units || !paths || !unknown) { status = SCWS_ENOMEM; goto done; }
	offset = 0;
	for (size_t i = 0; i < count; i++) {
		uint32_t ch;
		size_t size = scws_utf8_decode(text + offset, length - offset, &ch);
		units[i].offset = offset; units[i].ch = scws_ascii_width(ch);
		offset += size;
	}
	units[count].offset = length;
	atomize(units, count);
	scws_rule_numbers(units, count);
	for (size_t i = count; i-- > 0;) {
		uint32_t ch = units[i].ch;
		int space = scws_space(ch), symbol = scws_symbol(ch);
		paths[i].cost = UINT64_MAX;
		consider(paths, i, i + 1, space || symbol ? 0 : ctx->unknown_cost,
			!space && (!symbol || !!(ctx->flags & SCWS_KEEP_SYMBOLS)), symbol ? "un" : "", symbol ? SCWS_BUILTIN : SCWS_UNKNOWN);
		if (units[i].atom_end) consider(paths, i, units[i].atom_end, 8192, 1, units[i].atom_pos, SCWS_BUILTIN);
		if (units[i].number_to) {
			/* A quantified 每 expression must retain its measure unit. */
			if (units[i].ch != 0x6bcf) consider(paths, i, units[i].number_to, 8192, 1, "m", SCWS_BUILTIN);
			if (units[i].quantity_to > units[i].number_to)
				consider(paths, i, units[i].quantity_to, 8192, 1, "mq", SCWS_BUILTIN);
		}
		for (size_t j = i; j < count; j++) {
			size_t bytes = units[j + 1].offset - units[i].offset;
			scws_word word;
			if (bytes > 240 || boundary(units[j].ch)) break;
			if (lookup(ctx, text + units[i].offset, bytes, &word) && (word.flags & SCWS_WORD_MAIN))
				consider(paths, i, j + 1, word.cost, 1, word.pos, SCWS_DICTIONARY);
			if (!(word.flags & SCWS_WORD_PREFIX)) break;
		}
	}
	for (size_t i = 0; i < count; i = paths[i].to) {
		path *choice = &paths[i];
		if (units[i].number_to && (choice->to == units[i].number_to || choice->to == units[i].quantity_to))
			for (size_t j = i; j < choice->to; j++) units[j].numeric_owner = i + 1;
		if (choice->emit) {
			status = append(result, units, i, choice->to, choice->pos, SCWS_PRIMARY, choice->origin);
			if (status) goto done;
			if (choice->origin == SCWS_UNKNOWN && choice->to == i + 1 && scws_han(units[i].ch)) unknown[i] = 1;
		}
	}
	if (mode == SCWS_SEARCH) {
		size_t primary_count = result->count;
		for (size_t i = 0; i < count; i++) {
			size_t owner = units[i].numeric_owner;
			int mixed = 0;
			for (size_t j = i; j < count; j++) {
				scws_word word;
				int allowed = 1;
				size_t bytes = units[j + 1].offset - units[i].offset;
				if (bytes > 240 || boundary(units[j].ch)) break;
				if (units[j].numeric_owner != owner) mixed = 1;
				if (mixed) allowed = 0;
				else if (owner) {
					const unit *number = &units[owner - 1];
					size_t end = paths[owner - 1].to;
					allowed = (i == owner - 1 && j + 1 == end)
						|| (i == number->number_from && j + 1 == number->number_to)
						|| (i == number->number_to && j + 1 == end);
				}
				if (lookup(ctx, text + units[i].offset, bytes, &word) && j > i && allowed) {
					status = append(result, units, i, j + 1, word.pos, SCWS_SECONDARY, SCWS_DICTIONARY);
					if (status) goto done;
				}
				if (!(word.flags & SCWS_WORD_PREFIX)) break;
			}
			if (i + 1 < count && unknown[i] && unknown[i + 1] && (ctx->flags & SCWS_FALLBACK_BIGRAM)) {
				status = append(result, units, i, i + 2, "", SCWS_FALLBACK, SCWS_UNKNOWN);
				if (status) goto done;
			}
		}
		for (size_t t = 0; t < primary_count; t++) {
			/* append can realloc result->tokens, so copy all needed fields first. */
			scws_token_t token = result->tokens[t];
			size_t end = token.char_offset + token.char_length;
			const unit *first = &units[token.char_offset];
			if (first->number_to && (end == first->number_to || end == first->quantity_to)) {
				if (first->number_from != token.char_offset || first->number_to != end) {
					status = append(result, units, first->number_from, first->number_to, "m", SCWS_SECONDARY, SCWS_BUILTIN);
					if (status) goto done;
				}
				if (end > first->number_to) {
					status = append(result, units, first->number_to, end, "q", SCWS_SECONDARY, SCWS_BUILTIN);
					if (status) goto done;
				}
				continue;
			}
			if (token.origin != SCWS_BUILTIN || token.char_length < 2) continue;
			for (size_t i = token.char_offset, j; i < end; i = j) {
				int alpha = scws_alpha(units[i].ch), digit = scws_digit(units[i].ch);
				j = i + 1;
				if (!alpha && !digit) continue;
				while (j < end && (alpha ? scws_alpha(units[j].ch) : scws_digit(units[j].ch))) j++;
				if (i == token.char_offset && j == end) continue;
				status = append(result, units, i, j, alpha ? "en" : "m", SCWS_SECONDARY, SCWS_BUILTIN);
				if (status) goto done;
			}
		}
	}
	if (result->count > 1) {
		size_t used = 0;
		qsort(result->tokens, result->count, sizeof(*result->tokens), token_compare);
		for (size_t i = 0; i < result->count; i++) {
			if (used && result->tokens[used - 1].offset == result->tokens[i].offset
				&& result->tokens[used - 1].length == result->tokens[i].length) continue;
			result->tokens[used++] = result->tokens[i];
		}
		result->count = used;
	}
done:
	free(units); free(paths); free(unknown);
	if (status) { scws_result_free(result); result = NULL; }
	set_error(ctx, status);
	return result;
}
const scws_token_t *scws_result_next(scws_result_t result)
{
	return result && result->cursor < result->count ? &result->tokens[result->cursor++] : NULL;
}
size_t scws_result_count(scws_result_t result) { return result ? result->count : 0; }
void scws_result_free(scws_result_t result)
{
	if (result) { free(result->tokens); free(result); }
}
scws_status_t scws_error_code(scws_t ctx) { return ctx ? ctx->error : SCWS_EINVAL; }
const char *scws_strerror(scws_t ctx)
{
	static const char *const messages[] = {"success", "invalid argument", "out of memory", "I/O error",
		"invalid dictionary format", "invalid UTF-8", "resource limit exceeded", "operation not supported"};
	scws_status_t status = scws_error_code(ctx);
	return (unsigned) status < sizeof(messages) / sizeof(messages[0]) ? messages[status] : "unknown error";
}
