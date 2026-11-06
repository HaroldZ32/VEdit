// Negative control for the guard above: proves that a field named `signals`
// REALLY breaks a Qt translation unit, i.e. the guard is not vacuous.
#include <QObject>

struct Probe {
    int signals;
};
