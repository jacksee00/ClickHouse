#include "SettingsCommon.h"

#include <Core/Field.h>
#include <Common/getNumberOfPhysicalCPUCores.h>
#include <Common/FieldVisitors.h>
#include <IO/ReadHelpers.h>
#include <IO/ReadBufferFromString.h>
#include <IO/WriteHelpers.h>



namespace DB
{

namespace ErrorCodes
{
    extern const int TYPE_MISMATCH;
    extern const int UNKNOWN_LOAD_BALANCING;
    extern const int UNKNOWN_OVERFLOW_MODE;
    extern const int ILLEGAL_OVERFLOW_MODE;
    extern const int UNKNOWN_TOTALS_MODE;
    extern const int UNKNOWN_COMPRESSION_METHOD;
    extern const int UNKNOWN_DISTRIBUTED_PRODUCT_MODE;
    extern const int UNKNOWN_GLOBAL_SUBQUERIES_METHOD;
    extern const int UNKNOWN_JOIN_STRICTNESS;
    extern const int UNKNOWN_LOG_LEVEL;
    extern const int SIZE_OF_FIXED_STRING_DOESNT_MATCH;
    extern const int BAD_ARGUMENTS;
    extern const int UNKNOWN_SETTING;
    extern const int CANNOT_PARSE_BOOL;
}


template <typename Type>
String SettingNumber<Type>::toString() const
{
    return DB::toString(getValue());
}

template <typename Type>
Field SettingNumber<Type>::toField() const
{
    return getValue();
}

template <typename Type>
void SettingNumber<Type>::set(Type x)
{
    data.store(Data{x, true}, std::memory_order_relaxed);
}

template <typename Type>
void SettingNumber<Type>::set(const Field & x)
{
    if (x.getType() == Field::Types::String)
        set(get<const String &>(x));
    else
        set(applyVisitor(FieldVisitorConvertToNumber<Type>(), x));
}


template <typename Type>
SettingNumber<Type> & SettingNumber<Type>::operator= (const SettingNumber & o)
{
    data.store(o.data.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
}

template <typename Type>
void SettingNumber<Type>::set(const String & x)
{
    set(parse<Type>(x));
}

template <>
void SettingNumber<bool>::set(const String & x)
{
    if (x.size() == 1)
    {
        if (x[0] == '0')
            set(false);
        else if (x[0] == '1')
            set(true);
        else
            throw Exception("Cannot parse bool from string '" + x + "'", ErrorCodes::CANNOT_PARSE_BOOL);
    }
    else
    {
        ReadBufferFromString buf(x);
        if (checkStringCaseInsensitive("true", buf))
            set(true);
        else if (checkStringCaseInsensitive("false", buf))
            set(false);
        else
            throw Exception("Cannot parse bool from string '" + x + "'", ErrorCodes::CANNOT_PARSE_BOOL);
    }
}

template <typename Type>
void SettingNumber<Type>::serialize(WriteBuffer & buf) const
{
    if constexpr (std::is_integral_v<Type> && std::is_unsigned_v<Type>)
        writeVarUInt(static_cast<UInt64>(getValue()), buf);
    else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>)
        writeVarInt(static_cast<Int64>(getValue()), buf);
    else
    {
        static_assert(std::is_floating_point_v<Type>);
        writeBinary(toString(), buf);
    }
}

template <typename Type>
void SettingNumber<Type>::deserialize(ReadBuffer & buf)
{
    if constexpr (std::is_integral_v<Type> && std::is_unsigned_v<Type>)
    {
        UInt64 x;
        readVarUInt(x, buf);
        set(static_cast<Type>(x));
    }
    else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>)
    {
        Int64 x;
        readVarInt(x, buf);
        set(static_cast<Type>(x));
    }
    else
    {
        static_assert(std::is_floating_point_v<Type>);
        String x;
        readBinary(x, buf);
        set(x);
    }
}

template struct SettingNumber<UInt64>;
template struct SettingNumber<Int64>;
template struct SettingNumber<float>;
template struct SettingNumber<bool>;


SettingMaxThreads & SettingMaxThreads::operator= (const SettingMaxThreads & o)
{
    data.store(o.data.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
}

String SettingMaxThreads::toString() const
{
    auto d = data.load(std::memory_order_relaxed);
    /// Instead of the `auto` value, we output the actual value to make it easier to see.
    return d.is_auto ? ("auto(" + DB::toString(d.value) + ")") : DB::toString(d.value);
}

Field SettingMaxThreads::toField() const
{
    auto d = data.load(std::memory_order_relaxed);
    return d.is_auto ? 0 : d.value;
}

void SettingMaxThreads::set(UInt64 x)
{
    data.store({x ? x : getAutoValue(), x == 0, true});
}

void SettingMaxThreads::set(const Field & x)
{
    if (x.getType() == Field::Types::String)
        set(get<const String &>(x));
    else
        set(safeGet<UInt64>(x));
}

void SettingMaxThreads::set(const String & x)
{
    if (startsWith(x, "auto"))
        setAuto();
    else
        set(parse<UInt64>(x));
}

void SettingMaxThreads::serialize(WriteBuffer & buf) const
{
    auto d = data.load(std::memory_order_relaxed);
    writeVarUInt(d.is_auto ? 0 : d.value, buf);
}

void SettingMaxThreads::deserialize(ReadBuffer & buf)
{
    UInt64 x = 0;
    readVarUInt(x, buf);
    set(x);
}

void SettingMaxThreads::setAuto()
{
    data.store({getAutoValue(), true, isChanged()});
}

UInt64 SettingMaxThreads::getAutoValue() const
{
    static auto res = getNumberOfPhysicalCPUCores();
    return res;
}

void SettingMaxThreads::setChanged(bool changed)
{
    auto d = data.load(std::memory_order_relaxed);
    data.store({d.value, d.is_auto, changed});
}


template <SettingTimespanIO io_unit>
SettingTimespan<io_unit> & SettingTimespan<io_unit>::operator= (const SettingTimespan & o)
{
    std::shared_lock lock_o(o.mutex);
    value = o.value;
    changed = o.changed;
    return *this;
}

template <SettingTimespanIO io_unit>
SettingTimespan<io_unit>::SettingTimespan(const SettingTimespan & o)
{
    std::shared_lock lock_o(o.mutex);
    value = o.value;
    changed = o.changed;
}


template <SettingTimespanIO io_unit>
void SettingTimespan<io_unit>::setChanged(bool c)
{
    std::unique_lock lock(mutex);
    changed = c;
}

template <SettingTimespanIO io_unit>
String SettingTimespan<io_unit>::toString() const
{
    return DB::toString(getValue().totalMicroseconds() / microseconds_per_io_unit);
}

template <SettingTimespanIO io_unit>
Field SettingTimespan<io_unit>::toField() const
{
    return getValue().totalMicroseconds() / microseconds_per_io_unit;
}

template <SettingTimespanIO io_unit>
void SettingTimespan<io_unit>::set(const Poco::Timespan & x)
{
    std::unique_lock lock(mutex);
    value = x;
    changed = true;
}

template <SettingTimespanIO io_unit>
void SettingTimespan<io_unit>::set(UInt64 x)
{
    set(Poco::Timespan(x * microseconds_per_io_unit));
}

template <SettingTimespanIO io_unit>
void SettingTimespan<io_unit>::set(const Field & x)
{
    if (x.getType() == Field::Types::String)
        set(get<const String &>(x));
    else
        set(safeGet<UInt64>(x));
}

template <SettingTimespanIO io_unit>
void SettingTimespan<io_unit>::set(const String & x)
{
    set(parse<UInt64>(x));
}

template <SettingTimespanIO io_unit>
void SettingTimespan<io_unit>::serialize(WriteBuffer & buf) const
{
    writeVarUInt(getValue().totalMicroseconds() / microseconds_per_io_unit, buf);
}

template <SettingTimespanIO io_unit>
void SettingTimespan<io_unit>::deserialize(ReadBuffer & buf)
{
    UInt64 x = 0;
    readVarUInt(x, buf);
    set(x);
}

template struct SettingTimespan<SettingTimespanIO::SECOND>;
template struct SettingTimespan<SettingTimespanIO::MILLISECOND>;

SettingString & SettingString::operator= (const SettingString & o)
{
    std::shared_lock lock_o(o.mutex);
    value = o.value;
    changed = o.changed;
    return *this;
}

SettingString::SettingString(const SettingString & o)
{
    std::shared_lock lock(o.mutex);
    value = o.value;
    changed = o.changed;
}


String SettingString::toString() const
{
    std::shared_lock lock(mutex);
    return value;
}

Field SettingString::toField() const
{
    std::shared_lock lock(mutex);
    return value;
}

void SettingString::set(const String & x)
{
    std::unique_lock lock(mutex);
    value = x;
    changed = true;
}

void SettingString::setChanged(bool c)
{
    std::unique_lock lock(mutex);
    changed = c;
}

void SettingString::set(const Field & x)
{
    set(safeGet<const String &>(x));
}

void SettingString::serialize(WriteBuffer & buf) const
{
    writeBinary(value, buf);
}

void SettingString::deserialize(ReadBuffer & buf)
{
    String s;
    readBinary(s, buf);
    set(s);
}

SettingChar & SettingChar::operator= (const SettingChar & o)
{
    data.store(o.data.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
}

String SettingChar::toString() const
{
    return String(1, getValue());
}

Field SettingChar::toField() const
{
    return toString();
}

void SettingChar::set(char x)
{
    data.store({x, true});
}

void SettingChar::set(const String & x)
{
    if (x.size() > 1)
        throw Exception("A setting's value string has to be an exactly one character long", ErrorCodes::SIZE_OF_FIXED_STRING_DOESNT_MATCH);
    char c = (x.size() == 1) ? x[0] : '\0';
    set(c);
}

void SettingChar::set(const Field & x)
{
    const String & s = safeGet<const String &>(x);
    set(s);
}

void SettingChar::serialize(WriteBuffer & buf) const
{
    writeBinary(toString(), buf);
}

void SettingChar::deserialize(ReadBuffer & buf)
{
    String s;
    readBinary(s, buf);
    set(s);
}


template <typename EnumType, typename Tag>
SettingEnum<EnumType, Tag> & SettingEnum<EnumType, Tag>::operator= (const SettingEnum & o)
{
    data.store(o.data.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
}

template <typename EnumType, typename Tag>
void SettingEnum<EnumType, Tag>::set(EnumType x)
{
    data.store({x, true}, std::memory_order_relaxed);
}

template <typename EnumType, typename Tag>
void SettingEnum<EnumType, Tag>::serialize(WriteBuffer & buf) const
{
    writeBinary(toString(), buf);
}

template <typename EnumType, typename Tag>
void SettingEnum<EnumType, Tag>::deserialize(ReadBuffer & buf)
{
    String s;
    readBinary(s, buf);
    set(s);
}



#define IMPLEMENT_SETTING_ENUM(ENUM_NAME, LIST_OF_NAMES_MACRO, ERROR_CODE_FOR_UNEXPECTED_NAME) \
    IMPLEMENT_SETTING_ENUM_WITH_TAG(ENUM_NAME, void, LIST_OF_NAMES_MACRO, ERROR_CODE_FOR_UNEXPECTED_NAME)

#define IMPLEMENT_SETTING_ENUM_WITH_TAG(ENUM_NAME, TAG, LIST_OF_NAMES_MACRO, ERROR_CODE_FOR_UNEXPECTED_NAME) \
    template <> \
    String SettingEnum<ENUM_NAME, TAG>::toString() const \
    { \
        using EnumType = ENUM_NAME; \
        using UnderlyingType = std::underlying_type<EnumType>::type; \
        switch (static_cast<UnderlyingType>(getValue()))             \
        { \
            LIST_OF_NAMES_MACRO(IMPLEMENT_SETTING_ENUM_TO_STRING_HELPER_) \
        } \
        throw Exception("Unknown " #ENUM_NAME, ERROR_CODE_FOR_UNEXPECTED_NAME); \
    } \
    \
    template <> \
    void SettingEnum<ENUM_NAME, TAG>::set(const String & s) \
    { \
        using EnumType = ENUM_NAME; \
        LIST_OF_NAMES_MACRO(IMPLEMENT_SETTING_ENUM_FROM_STRING_HELPER_) \
        \
        String all_io_names; \
        LIST_OF_NAMES_MACRO(IMPLEMENT_SETTING_ENUM_CONCAT_NAMES_HELPER_) \
        throw Exception("Unknown " #ENUM_NAME " : '" + s + "', must be one of " + all_io_names, \
            ERROR_CODE_FOR_UNEXPECTED_NAME); \
    } \
    \
    template struct SettingEnum<ENUM_NAME, TAG>;

#define IMPLEMENT_SETTING_ENUM_TO_STRING_HELPER_(NAME, IO_NAME) \
    case static_cast<UnderlyingType>(EnumType::NAME): return IO_NAME;

#define IMPLEMENT_SETTING_ENUM_FROM_STRING_HELPER_(NAME, IO_NAME) \
    if (s == IO_NAME) \
    { \
        set(EnumType::NAME); \
        return; \
    }

#define IMPLEMENT_SETTING_ENUM_CONCAT_NAMES_HELPER_(NAME, IO_NAME) \
    if (!all_io_names.empty()) \
        all_io_names += ", "; \
    all_io_names += String("'") + IO_NAME + "'";


#define LOAD_BALANCING_LIST_OF_NAMES(M) \
    M(RANDOM, "random") \
    M(NEAREST_HOSTNAME, "nearest_hostname") \
    M(IN_ORDER, "in_order") \
    M(FIRST_OR_RANDOM, "first_or_random")
IMPLEMENT_SETTING_ENUM(LoadBalancing, LOAD_BALANCING_LIST_OF_NAMES, ErrorCodes::UNKNOWN_LOAD_BALANCING)


#define JOIN_STRICTNESS_LIST_OF_NAMES(M) \
    M(Unspecified, "") \
    M(ALL, "ALL") \
    M(ANY, "ANY")
IMPLEMENT_SETTING_ENUM(JoinStrictness, JOIN_STRICTNESS_LIST_OF_NAMES, ErrorCodes::UNKNOWN_JOIN_STRICTNESS)


#define TOTALS_MODE_LIST_OF_NAMES(M) \
    M(BEFORE_HAVING, "before_having") \
    M(AFTER_HAVING_EXCLUSIVE, "after_having_exclusive") \
    M(AFTER_HAVING_INCLUSIVE, "after_having_inclusive") \
    M(AFTER_HAVING_AUTO, "after_having_auto")
IMPLEMENT_SETTING_ENUM(TotalsMode, TOTALS_MODE_LIST_OF_NAMES, ErrorCodes::UNKNOWN_TOTALS_MODE)


#define OVERFLOW_MODE_LIST_OF_NAMES(M) \
    M(THROW, "throw") \
    M(BREAK, "break")
IMPLEMENT_SETTING_ENUM(OverflowMode, OVERFLOW_MODE_LIST_OF_NAMES, ErrorCodes::UNKNOWN_OVERFLOW_MODE)


#define OVERFLOW_MODE_LIST_OF_NAMES_WITH_ANY(M) \
    M(THROW, "throw") \
    M(BREAK, "break") \
    M(ANY, "any")
IMPLEMENT_SETTING_ENUM_WITH_TAG(OverflowMode, SettingOverflowModeGroupByTag, OVERFLOW_MODE_LIST_OF_NAMES_WITH_ANY, ErrorCodes::UNKNOWN_OVERFLOW_MODE)


#define DISTRIBUTED_PRODUCT_MODE_LIST_OF_NAMES(M) \
    M(DENY, "deny") \
    M(LOCAL, "local") \
    M(GLOBAL, "global") \
    M(ALLOW, "allow")
IMPLEMENT_SETTING_ENUM(DistributedProductMode, DISTRIBUTED_PRODUCT_MODE_LIST_OF_NAMES, ErrorCodes::UNKNOWN_DISTRIBUTED_PRODUCT_MODE)


#define DATE_TIME_INPUT_FORMAT_LIST_OF_NAMES(M) \
    M(Basic, "basic") \
    M(BestEffort, "best_effort")
IMPLEMENT_SETTING_ENUM(FormatSettings::DateTimeInputFormat, DATE_TIME_INPUT_FORMAT_LIST_OF_NAMES, ErrorCodes::BAD_ARGUMENTS)


#define LOGS_LEVEL_LIST_OF_NAMES(M) \
    M(none, "none") \
    M(error, "error") \
    M(warning, "warning") \
    M(information, "information") \
    M(debug, "debug") \
    M(trace, "trace")
IMPLEMENT_SETTING_ENUM(LogsLevel, LOGS_LEVEL_LIST_OF_NAMES, ErrorCodes::BAD_ARGUMENTS)


namespace details
{
    String SettingsCollectionUtils::deserializeName(ReadBuffer & buf)
    {
        String name;
        readBinary(name, buf);
        return name;
    }

    void SettingsCollectionUtils::serializeName(const StringRef & name, WriteBuffer & buf) { writeBinary(name, buf); }

    void SettingsCollectionUtils::throwNameNotFound(const StringRef & name)
    {
        throw Exception("Unknown setting " + name.toString(), ErrorCodes::UNKNOWN_SETTING);
    }
}
}
