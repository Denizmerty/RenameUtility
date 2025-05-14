#ifndef APP_H
#define APP_H

#include <wx/app.h>

class App : public wxApp
{
public:
	virtual bool OnInit() override;
};

#endif