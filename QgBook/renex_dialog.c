#include "pch.h"
#include "configs.h"
#include "doumi.h"

// 확장 파일 이름 바꾸기 대화상자
typedef struct RenEx
{
	GtkWindow* dialog;
	GtkWidget* name;
	GtkWidget* author;
	GtkWidget* title;
	GtkWidget* index;
	GtkWidget* extra;

	char extension[64];
	char filename[1024];
} RenEx;

// 만들기
static RenEx* renex_new(GtkWindow* parent, const char* filename)
{
	RenEx* self = g_new0(RenEx, 1);

	self->dialog = GTK_WINDOW(gtk_window_new());
	gtk_window_set_transient_for(self->dialog, parent);
	gtk_window_set_title(self->dialog, _("Rename book"));
	gtk_window_set_resizable(self->dialog, true);
	gtk_window_set_modal(self->dialog, true);
	gtk_window_set_default_size(self->dialog, 472, 240);
	gtk_widget_set_size_request(GTK_WIDGET(self->dialog), 472, 240);

	self->name = gtk_entry_new();
	self->author = gtk_entry_new();
	self->title = gtk_entry_new();
	self->index = gtk_entry_new();
	self->extra = gtk_entry_new();

	return self;
}
