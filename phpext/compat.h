#ifndef PHP_SCWS2_COMPAT_H
#define PHP_SCWS2_COMPAT_H

#if PHP_VERSION_ID < 70000
#error scws2 requires PHP 7.0 or newer
#endif

/* PHP 7.0/7.1 arginfo carries the return class in the type-info macro. */
#if PHP_VERSION_ID < 70200
#define SCWS2_ARG_RETURN_SELF(name, required) \
	ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(name, 0, required, IS_OBJECT, "self", 0)
#define SCWS2_ARG_RETURN_ARRAY(name, required) \
	ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(name, 0, required, IS_ARRAY, NULL, 0)
#else
#define SCWS2_ARG_RETURN_SELF(name, required) \
	ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(name, 0, required, self, 0)
#define SCWS2_ARG_RETURN_ARRAY(name, required) \
	ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(name, 0, required, IS_ARRAY, 0)
#endif

#if PHP_VERSION_ID >= 80000
#define SCWS2_OPTIONAL_INT(name, value) ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, name, IS_LONG, 0, value)
#define SCWS2_PROPERTY_ARGS zend_object *object, zend_string *member
#else
#define SCWS2_OPTIONAL_INT(name, value) ZEND_ARG_TYPE_INFO(0, name, IS_LONG, 0)
#define SCWS2_PROPERTY_ARGS zval *object, zval *member
#endif

#if PHP_VERSION_ID < 70400
#define SCWS2_WRITE_RETURN void
#define SCWS2_WRITE_DONE(value) return
#else
#define SCWS2_WRITE_RETURN zval *
#define SCWS2_WRITE_DONE(value) return value
#endif
#endif
