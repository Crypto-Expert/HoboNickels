#include "charitydialog.h"
#include "ui_charitydialog.h"

#include "walletmodel.h"
#include "base58.h"
#include "addressbookpage.h"
#include "init.h"


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
    if(model->getEncryptionStatus() == WalletModel::Locked)
    {
        ui->message->setStyleSheet("QLabel { color: black; }");
        ui->message->setText(tr("Please unlock wallet before starting stake for charity."));
        return;
    }

    bool fValidConversion = false;
    int64 nMinAmount = MIN_TXOUT_AMOUNT;
    int64 nMaxAmount = MAX_MONEY;

    CBitcoinAddress address = ui->charityAddressEdit->text().toStdString();
    if (!address.IsValid())
    {
        ui->message->setStyleSheet("QLabel { color: red; }");
        ui->message->setText(tr("The entered address: ") + ui->charityAddressEdit->text() + tr(" is invalid.\nPlease check the address and try again."));
        ui->charityAddressEdit->setFocus();
        return;
    }

    int nCharityPercent = ui->charityPercentEdit->text().toInt(&fValidConversion, 10);
    if (!fValidConversion || nCharityPercent > 50 || nCharityPercent <= 0)
    {
        ui->message->setStyleSheet("QLabel { color: red; }");
        ui->message->setText(tr("Please Enter 1 - 50 for percent."));
        ui->charityPercentEdit->setFocus();
        return;
    }

    if (!ui->charityMinEdit->text().isEmpty())
    {
        nMinAmount = ui->charityMinEdit->text().toDouble(&fValidConversion) * COIN;
        if(!fValidConversion || nMinAmount <= MIN_TXOUT_AMOUNT || nMinAmount >= MAX_MONEY  )
        {
            ui->message->setStyleSheet("QLabel { color: red; }");
            ui->message->setText(tr("Min Amount out of Range, please re-enter."));
            ui->charityMinEdit->setFocus();
            return;
        }
    }

    if (!ui->charityMaxEdit->text().isEmpty())
    {
        nMaxAmount = ui->charityMaxEdit->text().toDouble(&fValidConversion) * COIN;
        if(!fValidConversion || nMaxAmount <= MIN_TXOUT_AMOUNT || nMaxAmount >= MAX_MONEY  )
        {
            ui->message->setStyleSheet("QLabel { color: red; }");
            ui->message->setText(tr("Max Amount out of Range, please re-enter."));
            ui->charityMaxEdit->setFocus();
            return;
        }
    }

    model->setStakeForCharity(true, nCharityPercent, address, nMinAmount, nMaxAmount);
    if(!fGlobalStakeForCharity)
         fGlobalStakeForCharity = true;
    ui->message->setStyleSheet("QLabel { color: green; }");
    ui->message->setText(tr("Thank you for giving to\n") + QString(address.ToString().c_str()) + tr("."));
    return;
}

void StakeForCharityDialog::on_disableButton_clicked()
{
    int nCharityPercent = 0;
    CBitcoinAddress address = "";
    int64 nMinAmount = MIN_TXOUT_AMOUNT;
    int64 nMaxAmount = MAX_MONEY;

    model->setStakeForCharity(false, nCharityPercent, address, nMinAmount, nMaxAmount);
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
