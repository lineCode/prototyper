
/*!
	\file

	\author Igor Mironchik (igor.mironchik at gmail dot com).

	Copyright (c) 2016 Igor Mironchik

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Prototyper include.
#include "pdf_exporter.hpp"
#include "utils.hpp"

// Qt include.
#include <QPdfWriter>
#include <QPageLayout>
#include <QTextDocument>
#include <QList>
#include <QTemporaryFile>
#include <QSharedPointer>
#include <QSvgGenerator>
#include <QSvgRenderer>
#include <QTextCursor>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QPageSize>
#include <QBuffer>
#include <QByteArray>

// C++ include.
#include <algorithm>


namespace Prototyper {

namespace Core {

//
// PdfExporterPrivate
//

class PdfExporterPrivate
	:	public ExporterPrivate
{
public:
	PdfExporterPrivate( const Cfg::Project & cfg, PdfExporter * parent )
		:	ExporterPrivate( cfg, parent )
	{
	}

	//! Create tmp images.
	void createImages();

	//! Images' files.
	QList< QSharedPointer< QTemporaryFile > > m_images;
}; // class PdfExporterPrivate

void
PdfExporterPrivate::createImages()
{
	foreach( const Cfg::Form & form, m_cfg.form() )
	{
		m_images.append( QSharedPointer< QTemporaryFile >
			( new QTemporaryFile ) );

		m_images.last()->open();

		QSvgGenerator svg;
		svg.setFileName( m_images.last()->fileName() );
		svg.setResolution( 72 );

		drawForm( svg, form );
	}
}


//
// PdfExporter
//

PdfExporter::PdfExporter( const Cfg::Project & project )
	:	Exporter( QScopedPointer< ExporterPrivate >
			( new PdfExporterPrivate( project, this ) ) )
{
}

PdfExporter::~PdfExporter()
{
}

static const int c_pageBreakType = QTextFormat::UserFormat + 1;

void
PdfExporter::exportToDoc( const QString & fileName )
{
	PdfExporterPrivate * d = d_ptr();

	QPdfWriter pdf( fileName );

	pdf.setResolution( 300 );

	QPageLayout layout = pdf.pageLayout();
	layout.setUnits( QPageLayout::Point );
	const qreal margin = ( 2.0 / 2.54 ) * 72;
	layout.setMargins( QMarginsF( margin, margin,
		margin, margin ) );
	pdf.setPageLayout( layout );

	const QRectF body( 0, 0, pdf.width(), pdf.height() );

	d->createImages();

	QTextDocument doc;
	doc.setPageSize( body.size() );

	Cfg::fillTextDocument( &doc, d->m_cfg.description().text() );

	QTextCursor c( &doc );

	int i = 0;

	foreach( const Cfg::Form & form, d->m_cfg.form() )
	{
		c.movePosition( QTextCursor::End );

		c.insertText( QString( "\n" ) );

		c.movePosition( QTextCursor::End );

		QTextCharFormat fmt;
		fmt.setObjectType( c_pageBreakType );

		c.insertText( QString( QChar::ObjectReplacementCharacter ) +
			QString( "\n" ), fmt );

		c.movePosition( QTextCursor::End );

		QTextCharFormat header;
		header.setFontWeight( QFont::Bold );
		header.setFontPointSize( 20.0 );

		c.setCharFormat( header );

		c.insertText( form.tabName() + QLatin1String( "\n\n" ) );

		c.movePosition( QTextCursor::End );

		QTextImageFormat image;

		image.setName( d->m_images.at( i )->fileName() );

		++i;

		c.insertImage( image );

		c.movePosition( QTextCursor::End );

		c.insertText( QLatin1String( "\n" ) );

		c.movePosition( QTextCursor::End );

		auto formIt = std::find_if( form.desc().constBegin(),
			form.desc().constEnd(),
			[&form] ( const Cfg::Description & desc ) -> bool
				{
					return ( form.tabName() == desc.id() );
				}
		);

		if( formIt != form.desc().constEnd() )
		{
			c.insertText( QLatin1String( "\n\n" ) );

			c.movePosition( QTextCursor::End );

			Cfg::fillTextDocument( &doc, formIt->text() );

			c.movePosition( QTextCursor::End );

			c.insertText( QLatin1String( "\n\n" ) );

			c.movePosition( QTextCursor::End );
		}

		for( auto it = form.desc().constBegin(), last = form.desc().constEnd();
			it != last; ++it )
		{
			if( it != formIt )
			{
				QTextCharFormat h2;
				h2.setFontWeight( QFont::Bold );
				h2.setFontItalic( true );
				h2.setFontPointSize( 15.0 );

				c.setCharFormat( h2 );

				c.insertText( it->id() + QLatin1String( "\n\n" ) );

				c.movePosition( QTextCursor::End );

				Cfg::fillTextDocument( &doc, it->text() );

				c.movePosition( QTextCursor::End );

				c.insertText( QLatin1String( "\n\n" ) );

				c.movePosition( QTextCursor::End );
			}
		}
	}

	doc.documentLayout()->setPaintDevice( &pdf );

	doc.setPageSize( body.size() );

	QTextBlock block = doc.begin();

	QPainter p;
	p.begin( &pdf );

	qreal y = 0.0;

	while( block.isValid() )
	{
		QTextBlock::Iterator it = block.begin();

		QTextImageFormat imageFormat;

		bool isObject = false;
		bool isImage = false;
		bool isBreak = false;

		for( ; !it.atEnd(); ++it )
		{
			const QString txt = it.fragment().text();
			isObject = txt.contains(
				QChar::ObjectReplacementCharacter );
			isImage = isObject &&
				it.fragment().charFormat().isImageFormat();
			isBreak = isObject &&
				( it.fragment().charFormat().objectType() == c_pageBreakType );

			if( isImage )
				imageFormat = it.fragment().charFormat().toImageFormat();
		}

		if( isBreak )
		{
			pdf.newPage();

			y = 0.0;
		}
		else if( isImage )
		{
			QSvgRenderer svg( imageFormat.name() );
			const QSize s =
				svg.viewBox().size().scaled( QSize( body.size().width(),
					body.size().height() ), Qt::KeepAspectRatio );

			if( ( y + s.height() ) > body.height() )
			{
				pdf.newPage();

				y = 0.0;
			}

			p.save();

			p.translate( 0, y );

			svg.render( &p, QRectF( 0, 0, s.width(), s.height() ) );

			y += s.height();

			p.restore();
		}
		else
		{
			const QRectF r = block.layout()->boundingRect();

			block.layout()->setPosition( QPointF( 0.0, 0.0 ) );

			if( ( y + r.height() ) > body.height() )
			{
				pdf.newPage();

				y = 0.0;
			}

			block.layout()->draw( &p, QPointF( 0.0, y ) );

			y += r.height();
		}

		block = block.next();
	}

	p.end();
}

} /* namespace Core */

} /* namespace Prototyper */
