#pragma once

#include <string>
#include <zwidget/core/widget.h>

class Canvas;
class ImageBox;

class LauncherBanner : public Widget
{
public:
	LauncherBanner(Widget* parent);
	void UpdateLanguage();

	double GetPreferredHeight() const;

private:
	void OnGeometryChanged() override;
	void OnPaint(Canvas* canvas) override;

	ImageBox* Logo = nullptr;
	std::string VersionText;
};
