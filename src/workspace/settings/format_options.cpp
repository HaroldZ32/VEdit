#include "format_options.h"

#include "settings_client.h"

namespace vedit::workspace::settings {

FormatValues formatValuesFromEntries(const QVector<Entry> &entries)
{
    FormatValues values;
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        const QString key = QString::fromLatin1(knob.key);
        values.insert(key, SettingsClient::valueOf(entries, key));
    }
    return values;
}

void writeFormatEntries(QVector<Entry> *entries, const FormatValues &values)
{
    if (entries == nullptr) {
        return;
    }
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        const QString key = QString::fromLatin1(knob.key);
        // An empty value REMOVES the key: "unset" is how the store spells
        // "verible's own default", so a restored page leaves no residue in
        // settings.toml.
        SettingsClient::setValue(entries, key, values.value(key).trimmed());
    }
}

FormatValues defaultFormatValues()
{
    FormatValues values;
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        values.insert(QString::fromLatin1(knob.key), QString());
    }
    return values;
}

bool formatValuesAreDefault(const FormatValues &values)
{
    for (const FormatKnobSpec &knob : kFormatKnobs) {
        if (!values.value(QString::fromLatin1(knob.key)).trimmed().isEmpty()) {
            return false;
        }
    }
    return true;
}

} // namespace vedit::workspace::settings
