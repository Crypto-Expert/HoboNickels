#ifndef QCOMBOBOCFILTERCOINS_H
#define QCOMBOBOCFILTERCOINS_H
 
#include <QtGui>

//Derived Class from QComboBox
class QComboBoxFilterCoins: public QComboBox
{
  Q_OBJECT
public:
    QComboBoxFilterCoins(QWidget *parent = 0);

public slots:
    void handleSelectionChanged(int index);

signals:
    void valueChanged();
};
#endif // QCOMBOBOCFILTERCOINS_H
