#include "launcherbanner.h"
#include "gstrings.h"
#include "version.h"
#include <zwidget/core/canvas.h>
#include <zwidget/core/colorf.h>
#include <zwidget/core/image.h>
#include <zwidget/core/rect.h>
#include <zwidget/widgets/imagebox/imagebox.h>

LauncherBanner::LauncherBanner(Widget* parent) : Widget(parent)
{
	Logo = new ImageBox(this);
	Logo->SetImage(Image::LoadResource("widgets/banner.png"));
}

void LauncherBanner::UpdateLanguage()
{
	FString versionText = GStrings.GetString("PICKER_VERSION");
	versionText.Substitute("%s", GetVersionString());
	VersionText = versionText.GetChars();
	Update();
}

double LauncherBanner::GetPreferredHeight() const
{
	return Logo->GetPreferredHeight();
}

void LauncherBanner::OnGeometryChanged()
{
	Logo->SetFrameGeometry(0.0, 0.0, GetWidth(), Logo->GetPreferredHeight());
}

void LauncherBanner::OnPaint(Canvas* canvas)
{
	if (VersionText.empty())
	{
		return;
	}

	const Rect textBounds = canvas->measureText(VersionText);
	const double platePaddingX = 12.0;
	const double platePaddingY = 6.0;
	const double textX = GetWidth() - textBounds.width - 26.0;
	const double textBaseline = GetHeight() - 13.0;
	const Rect plate = Rect::xywh(
		textX - platePaddingX,
		GetHeight() - textBounds.height - (platePaddingY * 2.0) - 6.0,
		textBounds.width + platePaddingX * 2.0,
		textBounds.height + platePaddingY * 2.0);

	canvas->fillRect(plate, Colorf::fromRgba8(18, 10, 10, 180));
	canvas->drawText(Point(textX + 2.0, textBaseline + 2.0), Colorf::fromRgba8(24, 8, 8, 220), VersionText);
	canvas->drawText(Point(textX, textBaseline), Colorf::fromRgba8(235, 207, 156), VersionText);
}
