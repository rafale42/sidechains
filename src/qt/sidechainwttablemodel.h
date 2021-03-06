// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SIDECHAINWTTABLEMODEL_H
#define BITCOIN_SIDECHAINWTTABLEMODEL_H

#include <amount.h>
#include <uint256.h>

#include <QAbstractTableModel>
#include <QList>
#include <QString>

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

struct WTTableObject
{
    CAmount amount;
    CAmount amountMainchainFee;
    QString destination;
    unsigned int nCumulativeWeight;
    uint256 id;
    bool fMine;
};

class SidechainWTTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit SidechainWTTableModel(QObject *parent = 0);

    enum RoleIndex {
        /** WT ID */
        WTIDRole = Qt::UserRole,
        /** Is WT Mine? */
        IsMineRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;

    void setWalletModel(WalletModel *model);
    void setClientModel(ClientModel *model);

public Q_SLOTS:
    void UpdateModel();
    void SetOnlyMyWTs(bool fChecked);

private:
    QList<QVariant> model;
    QTimer *pollTimer;

    WalletModel *walletModel;
    ClientModel *clientModel;

    bool fOnlyMyWTs;
};

#endif // BITCOIN_SIDECHAINWTTABLEMODEL_H
