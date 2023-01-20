#include <dsmr.h>
#include <TimeLib.h>

using TelegramData = ParsedData<
    /* String */ identification,
    /* String */ p1_version,
    /* String */ timestamp,
    /* String */ equipment_id,
    /* FixedValue */ energy_delivered_tariff1,
    /* FixedValue */ energy_delivered_tariff2,
    /* FixedValue */ energy_returned_tariff1,
    /* FixedValue */ energy_returned_tariff2,
    /* String */ electricity_tariff,
    /* FixedValue */ power_delivered,
    /* FixedValue */ power_returned,
    /* FixedValue */ electricity_threshold,
    /* uint8_t */ electricity_switch_position,
    /* uint32_t */ electricity_failures,
    /* uint32_t */ electricity_long_failures,
    /* String */ electricity_failure_log,
    /* uint32_t */ electricity_sags_l1,
    /* uint32_t */ electricity_sags_l2,
    /* uint32_t */ electricity_sags_l3,
    /* uint32_t */ electricity_swells_l1,
    /* uint32_t */ electricity_swells_l2,
    /* uint32_t */ electricity_swells_l3,
    /* String */ message_short,
    /* String */ message_long,
    /* FixedValue */ voltage_l1,
    /* FixedValue */ voltage_l2,
    /* FixedValue */ voltage_l3,
    /* FixedValue */ current_l1,
    /* FixedValue */ current_l2,
    /* FixedValue */ current_l3,
    /* FixedValue */ power_delivered_l1,
    /* FixedValue */ power_delivered_l2,
    /* FixedValue */ power_delivered_l3,
    /* FixedValue */ power_returned_l1,
    /* FixedValue */ power_returned_l2,
    /* FixedValue */ power_returned_l3,
    /* uint16_t */ gas_device_type,
    /* String */ gas_equipment_id,
    /* uint8_t */ gas_valve_position,
    /* TimestampedFixedValue */ gas_delivered,
    /* uint16_t */ thermal_device_type,
    /* String */ thermal_equipment_id,
    /* uint8_t */ thermal_valve_position,
    /* TimestampedFixedValue */ thermal_delivered,
    /* uint16_t */ water_device_type,
    /* String */ water_equipment_id,
    /* uint8_t */ water_valve_position,
    /* TimestampedFixedValue */ water_delivered,
    /* uint16_t */ sub_device_type,
    /* String */ sub_equipment_id,
    /* uint8_t */ sub_valve_position,
    /* TimestampedFixedValue */ sub_delivered>;

int toMajorVersion(String versionString)
{
    return versionString.substring(0, 1).toInt();
}

long toUnixTime(String timestamp)
{
    // (YYMMDDhhmmssX) Date-time stamp of the P1 message (X=S Summer time, X=W Winter time)

    tmElements_t te;
    te.Year = ("20" + timestamp.substring(0, 2)).toInt() - 1970;
    te.Month = timestamp.substring(2, 4).toInt();
    te.Day = timestamp.substring(4, 6).toInt();
    te.Hour = timestamp.substring(6, 8).toInt();
    te.Minute = timestamp.substring(8, 10).toInt();
    te.Second = timestamp.substring(10, 12).toInt();

    time_t unixTime;
    unixTime = makeTime(te);

    return unixTime;
}

int getGasReportedPeriod(TelegramData data)
{
    int major = toMajorVersion(data.p1_version);
    if (major < 5)
    {
        return 60;
    }
    else
    {
        return 5;
    }
}