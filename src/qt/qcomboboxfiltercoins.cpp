#include "qcomboboxfiltercoins.h"
#include <QtGui>

QComboBoxFilterCoins::QComboBoxFilterCoins(QWidget *parent) :
        QComboBox(parent), role(Qt::UserRole)
{
    connect(this, SIGNAL(currentIndexChanged(int)), this, SLOT(handleSelectionChanged(int)));
}

QVariant QComboBoxFilterCoins::value() const
{
    return itemData(currentIndex(), role);
}

void QComboBoxFilterCoins::setValue(const QVariant &value)
{
    setCurrentIndex(findData(value, role));
}

void QComboBoxFilterCoins::setRole(int role)
{
    this->role = role;
}

void QComboBoxFilterCoins::handleSelectionChanged(int idx)
{
    emit valueChanged();
}
