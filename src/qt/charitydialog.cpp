#include "charitydialog.h"
#include "ui_charitydialog.h"

#include "walletmodel.h"
#include "base58.h"
#include "addressbookpage.h"



StakeForCharityDialog::StakeForCharityDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::StakeForCharityDialog),
    model(0)
{
    ui->setupUi(this);

    ui->label_2->setFocus();

}

StakeForCharityDialog::~StakeForCharityDialog()
{
    delete ui;
}

void StakeForCharityDialog::setModel(WalletModel *model)
{
    this->model = model;
}

void StakeForCharityDialog::setAddress(const QString &address)
{
    ui->charityAddressEdit->setText(address);
    ui->charityAddressEdit->setFocus();
}

void StakeForCharityDialog::on_addressBookButton_clicked()
{
    if (model && model->getAddressTableModel())
    {
        AddressBookPage dlg(AddressBookPage::ForSending, AddressBookPage::SendingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if (dlg.exec())
        {
            setAddress(dlg.getReturnValue());
        }
    }
}

void StakeForCharityDialog::on_enableButton_clicked()
{
    CBitcoinAddress address = ui->charityAddressEdit->text().toStdString();

    if(model->getEncryptionStatus() == WalletModel::Locked)
    {
        ui->message->setStyleSheet("QLabel { color: black; }");
        ui->message->setText(tr("Please unlock wallet before starting stake for charity."));
        return;
    }

    if (!address.IsValid())
    {
        ui->message->setStyleSheet("QLabel { color: red; }");
        ui->message->setText(tr("The entered address: ") + ui->charityAddressEdit->text() + tr(" is invalid.\nPlease check the address and try again."));
        return;
    }

    QString str = ui->charityPercentEdit->text();
    bool fIntConversion;
    int nCharityPercent = str.toInt(&fIntConversion, 10);
    if (!fIntConversion || nCharityPercent > 50 || nCharityPercent <= 0)
    {
        ui->message->setStyleSheet("QLabel { color: red; }");
        ui->message->setText(tr("Please Enter 1 - 50 for percent."));
        ui->charityPercentEdit->clear();
        return;
    }

     model->setStakeForCharity(true, nCharityPercent, address);
     ui->message->setStyleSheet("QLabel { color: green; }");
     ui->message->setText(tr("Thank you for giving to\n") + QString(address.ToString().c_str()) + tr("."));
     return;
}

void StakeForCharityDialog::on_disableButton_clicked()
{
    model->setStakeForCharity(false,0,"");
    ui->charityAddressEdit->clear();
    ui->charityMaxEdit->clear();
    ui->charityMinEdit->clear();
    ui->charityPercentEdit->clear();
    ui->message->setStyleSheet("QLabel { color: black; }");
    ui->message->setText(tr("Stake for Charity is now off"));
    return;
}

void StakeForCharityDialog::on_closeButton_clicked()
{
    close();
}
