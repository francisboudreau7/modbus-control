// Renders an SVG to a square PNG: render_icon <in.svg> <out.png> <size>
// Used by tools/make_icons.ps1 to produce the application icon images (not part of the app).

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv); // fonts for <text>
    if (argc != 4) {
        std::fprintf(stderr, "usage: render_icon <in.svg> <out.png> <size>\n");
        return 2;
    }
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    const int size = QString::fromLocal8Bit(argv[3]).toInt();
    if (!svg.isValid() || size <= 0) {
        std::fprintf(stderr, "invalid SVG or size\n");
        return 1;
    }
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    svg.render(&painter, QRectF(0, 0, size, size));
    painter.end();
    return image.save(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
}
