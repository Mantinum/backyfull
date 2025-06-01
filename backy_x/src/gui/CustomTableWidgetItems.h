#ifndef CUSTOMTABLEWIDGETITEMS_H
#define CUSTOMTABLEWIDGETITEMS_H

#include <QTableWidgetItem>
#include <QVariant>
#include <QString> // Required for QString text() comparison
#include <QDateTime> // Required for QDateTime conversion in DateTimeTableWidgetItem if more complex logic is needed

// For qlonglong
#include <QtGlobal>

class SizeTableWidgetItem : public QTableWidgetItem {
public:
    SizeTableWidgetItem(const QString &text = QString()) : QTableWidgetItem(text) {}
    SizeTableWidgetItem(const QString &text, qlonglong rawSize) : QTableWidgetItem(text) {
        setData(Qt::UserRole, QVariant::fromValue(rawSize));
    }

    bool operator<(const QTableWidgetItem &other) const override {
        // Handle ".." entry: always sort ".." first when ascending.
        // For descending, it should be last. This simple check works for ascending.
        // A more complete solution handles sort order (this->tableWidget()->horizontalHeader()->sortIndicatorOrder()).
        if (text() == "..") return true;
        if (other.text() == "..") return false;

        QVariant myData = data(Qt::UserRole);
        QVariant otherData = other.data(Qt::UserRole);

        if (myData.isValid() && otherData.isValid()) {
            bool myIsNumeric = myData.canConvert<qlonglong>();
            bool otherIsNumeric = otherData.canConvert<qlonglong>();

            // Treat actual numbers as greater than placeholder strings like "-" (which represent directories)
            // when sorting in ascending order. So, directories come before files if just based on this.
            if (myIsNumeric && !otherIsNumeric) { // This item is a number (file size), other is not (dir)
                return false; // Files (numbers) are "greater than" directories ("-") in ascending
            }
            if (!myIsNumeric && otherIsNumeric) { // This item is not a number (dir), other is (file size)
                return true;  // Directories ("-") are "less than" files (numbers) in ascending
            }
            if (!myIsNumeric && !otherIsNumeric) { // Both are non-numeric (e.g. two directories with "-")
                return text() < other.text(); // Sort by display text
            }
            // Both are numeric, sort by the qlonglong value
            return myData.toLongLong() < otherData.toLongLong();
        }
        // Fallback for items without UserRole data (should ideally not happen for size column)
        return QTableWidgetItem::operator<(other);
    }
};

class DateTimeTableWidgetItem : public QTableWidgetItem {
public:
    DateTimeTableWidgetItem(const QString &text = QString()) : QTableWidgetItem(text) {}
    DateTimeTableWidgetItem(const QString &text, qlonglong timestamp) : QTableWidgetItem(text) {
        setData(Qt::UserRole, QVariant::fromValue(timestamp));
    }

    bool operator<(const QTableWidgetItem &other) const override {
        // Handle ".." entry
        if (text() == ".." || data(Qt::UserRole).toLongLong() == 0 ) { // Assuming timestamp 0 for ".."
             // For ascending sort, ".." (or items with timestamp 0) should come first.
            return true;
        }
        if (other.text() == ".." || other.data(Qt::UserRole).toLongLong() == 0) {
            return false;
        }

        QVariant myData = data(Qt::UserRole);
        QVariant otherData = other.data(Qt::UserRole);

        if (myData.isValid() && otherData.isValid() && myData.canConvert<qlonglong>() && otherData.canConvert<qlonglong>()) {
            return myData.toLongLong() < otherData.toLongLong();
        }
        // Fallback for items without valid UserRole data or if one is ".." and not handled above.
        return QTableWidgetItem::operator<(other);
    }
};

#endif // CUSTOMTABLEWIDGETITEMS_H
