#ifndef CHARITYDIALOG_H
#define CHARITYDIALOG_H

#include <QDialog>

namespace Ui {
    class StakeForCharityDialog;
}
class WalletModel;

QT_BEGIN_NAMESPACE
QT_END_NAMESPACE

class StakeForCharityDialog : public QDialog
{
    Q_OBJECT

public:
    explicit StakeForCharityDialog(QWidget *parent = 0);
    ~StakeForCharityDialog();

    void setModel(WalletModel *model);
    void setAddress(const QString &address);
private slots:
    void on_enableButton_clicked();
    void on_disableButton_clicked();
    void on_closeButton_clicked();
    void on_addressBookButton_clicked();


private:
    Ui::StakeForCharityDialog *ui;
    WalletModel *model;
};

#endif // CHARITYDIALOG_H
