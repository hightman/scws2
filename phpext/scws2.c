/* scws2 PHP object interface. See COPYING for the BSD license. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "php_scws2.h"
#include "compat.h"
#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "ext/spl/spl_exceptions.h"
#include "ext/standard/info.h"
#include "main/fopen_wrappers.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static zend_class_entry *scws2_ce;
static zend_object_handlers scws2_handlers;
typedef struct { scws2_t ctx; zend_object std; } php_scws2_object;
#if defined(ZTS) && defined(COMPILE_DL_SCWS2)
ZEND_TSRMLS_CACHE_DEFINE()
#endif

static php_scws2_object *scws2_object(zend_object *object)
{
	return (php_scws2_object *) ((char *) object - offsetof(php_scws2_object, std));
}
static scws2_t scws2_context(zval *self)
{
	scws2_t ctx = scws2_object(Z_OBJ_P(self))->ctx;
	if (!ctx) zend_throw_exception(spl_ce_LogicException, "Scws2 is not initialized", 0);
	return ctx;
}
static void scws2_throw(scws2_status_t status, const char *method, const char *message)
{
	zend_class_entry *exception = status == SCWS2_EINVAL || status == SCWS2_EUTF8
		? spl_ce_InvalidArgumentException : spl_ce_RuntimeException;
	zend_throw_exception_ex(exception, status, "Scws2::%s(): %s", method, message);
}
static zend_object *scws2_create(zend_class_entry *ce)
{
	php_scws2_object *object = ecalloc(1, sizeof(*object) + zend_object_properties_size(ce));
	zend_object_std_init(&object->std, ce);
	object_properties_init(&object->std, ce);
	object->std.handlers = &scws2_handlers;
	return &object->std;
}
static void scws2_destroy(zend_object *std)
{
	php_scws2_object *object = scws2_object(std);
	scws2_free(object->ctx);
	object->ctx = NULL;
	zend_object_std_dtor(std);
}
static SCWS2_WRITE_RETURN scws2_write_property(SCWS2_PROPERTY_ARGS, zval *value, void **cache_slot)
{
	(void) object; (void) member; (void) cache_slot; (void) value;
	zend_throw_exception(spl_ce_LogicException, "Scws2 does not allow properties", 0);
	SCWS2_WRITE_DONE(value);
}
static zval *scws2_property_ptr(SCWS2_PROPERTY_ARGS, int type, void **cache_slot)
{
	(void) object; (void) member; (void) type; (void) cache_slot;
	/* Route indirect writes through read_property, without creating a property. */
	return NULL;
}
static zval *scws2_read_property(SCWS2_PROPERTY_ARGS, int type, void **cache_slot, zval *rv)
{
	if (type != BP_VAR_R && type != BP_VAR_IS) {
		zend_throw_exception(spl_ce_LogicException, "Scws2 does not allow properties", 0);
		return &EG(uninitialized_zval);
	}
	return zend_get_std_object_handlers()->read_property(object, member, type, cache_slot, rv);
}
static int scws2_text_path(const char *path, size_t length)
{
	const char *suffix;
	if (length < 4) return 0;
	suffix = path + length - 4;
	return suffix[0] == '.' && (suffix[1] == 't' || suffix[1] == 'T')
		&& (suffix[2] == 'x' || suffix[2] == 'X') && (suffix[3] == 't' || suffix[3] == 'T');
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 0)
ZEND_END_ARG_INFO()
SCWS2_ARG_RETURN_SELF(arginfo_dict, 1)
	ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
	SCWS2_OPTIONAL_INT(flags, "0")
ZEND_END_ARG_INFO()
SCWS2_ARG_RETURN_SELF(arginfo_options, 1)
	ZEND_ARG_TYPE_INFO(0, flags, IS_LONG, 0)
ZEND_END_ARG_INFO()
SCWS2_ARG_RETURN_ARRAY(arginfo_segment, 1)
	ZEND_ARG_TYPE_INFO(0, text, IS_STRING, 0)
	SCWS2_OPTIONAL_INT(mode, "Scws2::STANDARD")
ZEND_END_ARG_INFO()

PHP_METHOD(Scws2, __construct)
{
	php_scws2_object *object;
	(void) return_value;
	if (zend_parse_parameters_throw(ZEND_NUM_ARGS(), "") == FAILURE) return;
	object = scws2_object(Z_OBJ_P(getThis()));
	if (object->ctx) {
		zend_throw_exception(spl_ce_LogicException, "Scws2 is already initialized", 0);
		return;
	}
	object->ctx = scws2_new();
	if (!object->ctx) scws2_throw(SCWS2_ENOMEM, "__construct", "out of memory");
}

static void scws2_dictionary(INTERNAL_FUNCTION_PARAMETERS, int replace)
{
	char *path, expanded[MAXPATHLEN];
	size_t length;
	zend_long flags = 0;
	scws2_t ctx;
	scws2_dict_flags_t loading;
	scws2_status_t status;
	const char *method = replace ? "setDict" : "addDict";
	if (zend_parse_parameters_throw(ZEND_NUM_ARGS(), "s|l", &path, &length, &flags) == FAILURE) return;
	if (!(ctx = scws2_context(getThis()))) return;
	if (!length || memchr(path, '\0', length) || strstr(path, "://") || flags < 0 || flags > SCWS2_DICT_MEMORY) {
		scws2_throw(SCWS2_EINVAL, method, "invalid local path or dictionary flags"); return;
	}
	if (php_check_open_basedir(path)) {
		if (!EG(exception)) scws2_throw(SCWS2_EIO, method, "dictionary path is outside open_basedir");
		return;
	}
	if (!expand_filepath(path, expanded)) {
		scws2_throw(SCWS2_EIO, method, "cannot resolve dictionary path"); return;
	}
	if (php_check_open_basedir(expanded)) {
		if (!EG(exception)) scws2_throw(SCWS2_EIO, method, "dictionary path is outside open_basedir");
		return;
	}
	loading = (scws2_dict_flags_t) flags;
	if (scws2_text_path(path, length)) loading |= SCWS2_DICT_TEXT;
	status = replace ? scws2_set_dict(ctx, expanded, loading) : scws2_add_dict(ctx, expanded, loading);
	if (status) { scws2_throw(status, method, scws2_strerror(ctx)); return; }
	RETURN_ZVAL(getThis(), 1, 0);
}
PHP_METHOD(Scws2, setDict) { scws2_dictionary(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1); }
PHP_METHOD(Scws2, addDict) { scws2_dictionary(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0); }

PHP_METHOD(Scws2, setOptions)
{
	zend_long flags;
	scws2_t ctx;
	scws2_status_t status;
	if (zend_parse_parameters_throw(ZEND_NUM_ARGS(), "l", &flags) == FAILURE) return;
	if (!(ctx = scws2_context(getThis()))) return;
	if (flags < 0 || flags > (SCWS2_KEEP_SYMBOLS | SCWS2_FALLBACK_BIGRAM)) {
		scws2_throw(SCWS2_EINVAL, "setOptions", "invalid option flags"); return;
	}
	status = scws2_set_options(ctx, (unsigned) flags);
	if (status) { scws2_throw(status, "setOptions", scws2_strerror(ctx)); return; }
	RETURN_ZVAL(getThis(), 1, 0);
}

PHP_METHOD(Scws2, segment)
{
	char *text;
	size_t length, count;
	zend_long mode = SCWS2_STANDARD;
	scws2_t ctx;
	scws2_result_t result;
	const scws2_token_t *token;
	if (zend_parse_parameters_throw(ZEND_NUM_ARGS(), "s|l", &text, &length, &mode) == FAILURE) return;
	if (!(ctx = scws2_context(getThis()))) return;
	if (mode != SCWS2_STANDARD && mode != SCWS2_SEARCH) {
		scws2_throw(SCWS2_EINVAL, "segment", "invalid segmentation mode"); return;
	}
	result = scws2_segment(ctx, text, length, (scws2_mode_t) mode);
	if (!result) { scws2_throw(scws2_error_code(ctx), "segment", scws2_strerror(ctx)); return; }
	count = scws2_result_count(result);
	/* Conservatively keep preallocation and indexes representable on 32-bit PHP. */
	if (count > INT_MAX || count > HT_MAX_SIZE) {
		scws2_result_free(result);
		scws2_throw(SCWS2_ELIMIT, "segment", "too many tokens for a PHP array"); return;
	}
	/* Zend OOM bails out instead of returning: release native memory before rethrow. */
	zend_try {
		array_init_size(return_value, (uint32_t) count);
		while ((token = scws2_result_next(result)) != NULL) {
			zval row;
			if (token->offset > ZEND_LONG_MAX || token->length > ZEND_LONG_MAX
				|| token->char_offset > ZEND_LONG_MAX || token->char_length > ZEND_LONG_MAX) {
				zval_ptr_dtor(return_value); ZVAL_UNDEF(return_value);
				scws2_throw(SCWS2_ELIMIT, "segment", "token span exceeds PHP integer range");
				break;
			}
			array_init_size(&row, 8);
			/* Attach immediately so the request owns partially constructed rows. */
			add_next_index_zval(return_value, &row);
			add_assoc_stringl(&row, "word", text + token->offset, token->length);
			add_assoc_long(&row, "offset", (zend_long) token->offset);
			add_assoc_long(&row, "length", (zend_long) token->length);
			add_assoc_long(&row, "char_offset", (zend_long) token->char_offset);
			add_assoc_long(&row, "char_length", (zend_long) token->char_length);
			add_assoc_string(&row, "pos", (char *) token->pos);
			add_assoc_long(&row, "kind", token->kind);
			add_assoc_long(&row, "origin", token->origin);
		}
	} zend_catch {
		scws2_result_free(result);
		zend_bailout();
	} zend_end_try();
	scws2_result_free(result);
}

static const zend_function_entry scws2_methods[] = {
	PHP_ME(Scws2, __construct, arginfo_construct, ZEND_ACC_PUBLIC)
	PHP_ME(Scws2, setDict, arginfo_dict, ZEND_ACC_PUBLIC)
	PHP_ME(Scws2, addDict, arginfo_dict, ZEND_ACC_PUBLIC)
	PHP_ME(Scws2, setOptions, arginfo_options, ZEND_ACC_PUBLIC)
	PHP_ME(Scws2, segment, arginfo_segment, ZEND_ACC_PUBLIC)
	PHP_FE_END
};

PHP_MINIT_FUNCTION(scws2)
{
	zend_class_entry ce;
	(void) type; (void) module_number;
	INIT_CLASS_ENTRY(ce, "Scws2", scws2_methods);
	scws2_ce = zend_register_internal_class(&ce);
#if PHP_VERSION_ID < 70200
	/* The older VM precheck uses the internal function's strict flag, coercing
	 * scalars before our parser sees them. Keep reflected types, but let the
	 * parameter parser validate against the actual caller's strict_types. */
	{
		zend_function *method;
		ZEND_HASH_FOREACH_PTR(&scws2_ce->function_table, method) {
			method->common.fn_flags &= ~ZEND_ACC_HAS_TYPE_HINTS;
		} ZEND_HASH_FOREACH_END();
	}
#endif
	scws2_ce->ce_flags |= ZEND_ACC_FINAL;
	scws2_ce->create_object = scws2_create;
#if PHP_VERSION_ID >= 80100
	scws2_ce->ce_flags |= ZEND_ACC_NOT_SERIALIZABLE;
#else
	scws2_ce->serialize = zend_class_serialize_deny;
	scws2_ce->unserialize = zend_class_unserialize_deny;
#endif
#ifdef ZEND_ACC_NO_DYNAMIC_PROPERTIES
	scws2_ce->ce_flags |= ZEND_ACC_NO_DYNAMIC_PROPERTIES;
#endif
	memcpy(&scws2_handlers, zend_get_std_object_handlers(), sizeof(scws2_handlers));
	scws2_handlers.offset = offsetof(php_scws2_object, std);
	scws2_handlers.free_obj = scws2_destroy;
	scws2_handlers.clone_obj = NULL;
	scws2_handlers.write_property = scws2_write_property;
	scws2_handlers.get_property_ptr_ptr = scws2_property_ptr;
	scws2_handlers.read_property = scws2_read_property;
#define SCWS2_CONSTANT(name, value) zend_declare_class_constant_long(scws2_ce, name, sizeof(name) - 1, value)
	SCWS2_CONSTANT("DICT_MEMORY", SCWS2_DICT_MEMORY);
	SCWS2_CONSTANT("STANDARD", SCWS2_STANDARD);
	SCWS2_CONSTANT("SEARCH", SCWS2_SEARCH);
	SCWS2_CONSTANT("KEEP_SYMBOLS", SCWS2_KEEP_SYMBOLS);
	SCWS2_CONSTANT("FALLBACK_BIGRAM", SCWS2_FALLBACK_BIGRAM);
	SCWS2_CONSTANT("PRIMARY", SCWS2_PRIMARY);
	SCWS2_CONSTANT("SECONDARY", SCWS2_SECONDARY);
	SCWS2_CONSTANT("FALLBACK", SCWS2_FALLBACK);
	SCWS2_CONSTANT("DICTIONARY", SCWS2_DICTIONARY);
	SCWS2_CONSTANT("BUILTIN", SCWS2_BUILTIN);
	SCWS2_CONSTANT("UNKNOWN", SCWS2_UNKNOWN);
	SCWS2_CONSTANT("EINVAL", SCWS2_EINVAL);
	SCWS2_CONSTANT("ENOMEM", SCWS2_ENOMEM);
	SCWS2_CONSTANT("EIO", SCWS2_EIO);
	SCWS2_CONSTANT("EFORMAT", SCWS2_EFORMAT);
	SCWS2_CONSTANT("EUTF8", SCWS2_EUTF8);
	SCWS2_CONSTANT("ELIMIT", SCWS2_ELIMIT);
	SCWS2_CONSTANT("ENOTSUP", SCWS2_ENOTSUP);
#undef SCWS2_CONSTANT
	return SUCCESS;
}
PHP_RINIT_FUNCTION(scws2)
{
	(void) type; (void) module_number;
#if defined(ZTS) && defined(COMPILE_DL_SCWS2)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}
PHP_MINFO_FUNCTION(scws2)
{
	(void) zend_module;
	php_info_print_table_start();
	php_info_print_table_row(2, "scws2 support", "enabled");
	php_info_print_table_row(2, "Version", PHP_SCWS2_VERSION);
	php_info_print_table_row(2, "Encoding", "UTF-8");
	php_info_print_table_row(2, "Dictionary loading", "mmap by default, memory fallback");
#if !defined(SCWS2_NO_TEXT_CACHE) && (defined(__unix__) || defined(__APPLE__))
	php_info_print_table_row(2, "Text dictionary disk cache", "enabled (managed by libscws2)");
#else
	php_info_print_table_row(2, "Text dictionary disk cache", "disabled at build time");
#endif
	php_info_print_table_end();
}
static const zend_module_dep scws2_deps[] = {
	ZEND_MOD_REQUIRED("spl")
	ZEND_MOD_END
};
zend_module_entry scws2_module_entry = {
	STANDARD_MODULE_HEADER_EX, NULL, scws2_deps,
	"scws2", NULL, PHP_MINIT(scws2), NULL, PHP_RINIT(scws2), NULL,
	PHP_MINFO(scws2), PHP_SCWS2_VERSION, STANDARD_MODULE_PROPERTIES
};
#ifdef COMPILE_DL_SCWS2
ZEND_GET_MODULE(scws2)
#endif
