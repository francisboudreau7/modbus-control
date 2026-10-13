#include "RichText.h"

namespace mb::rich {

QString escaped(const QString &plain)
{
    return plain.toHtmlEscaped();
}

QString bold(const QString &plain)
{
    return QStringLiteral("<b>%1</b>").arg(escaped(plain));
}

QString headline(const QString &plain)
{
    return QStringLiteral("<p><b>%1</b></p>").arg(escaped(plain));
}

QString paragraph(const QString &plain)
{
    return paragraphRich(escaped(plain));
}

QString paragraphRich(const QString &rich)
{
    return QStringLiteral("<p>%1</p>").arg(rich);
}

QString note(const QString &plain)
{
    return QStringLiteral("<p><small>%1</small></p>").arg(escaped(plain));
}

QString rows(const QList<Row> &rows)
{
    QString html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"3\">");
    for (const Row &row : rows) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                    .arg(escaped(row.label), row.emphasize ? bold(row.value) : escaped(row.value));
    }
    return html + QStringLiteral("</table>");
}

QString grid(const QStringList &header, const QList<QStringList> &cells, int emphasizeColumn)
{
    QString html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"3\"><tr>");
    for (const QString &h : header)
        html += QStringLiteral("<th align=\"left\">%1</th>").arg(escaped(h));
    html += QStringLiteral("</tr>");
    for (const QStringList &line : cells) {
        html += QStringLiteral("<tr>");
        for (qsizetype c = 0; c < line.size(); ++c) {
            html += QStringLiteral("<td>%1</td>")
                        .arg(c == emphasizeColumn ? bold(line.at(c)) : escaped(line.at(c)));
        }
        html += QStringLiteral("</tr>");
    }
    return html + QStringLiteral("</table>");
}

QString bulletsRich(const QStringList &rich)
{
    QString html = QStringLiteral("<ul>");
    for (const QString &item : rich)
        html += QStringLiteral("<li>%1</li>").arg(item);
    return html + QStringLiteral("</ul>");
}

QString bullets(const QStringList &plain)
{
    QStringList items;
    for (const QString &p : plain)
        items << escaped(p);
    return bulletsRich(items);
}

} // namespace mb::rich
