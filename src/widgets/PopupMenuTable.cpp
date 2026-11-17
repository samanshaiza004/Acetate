/**********************************************************************

Audacity: A Digital Audio Editor

PopupMenuTable.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/


#include "PopupMenuTable.h"
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
#include "AlicornMenu.h"
#endif
#include "widgets/BasicMenu.h"
#include "wxWidgetsWindowPlacement.h"

PopupMenuTableEntry::~PopupMenuTableEntry()
{}

PopupSubMenu::PopupSubMenu(const Identifier &stringId,
   const TranslatableString &caption, PopupMenuTable &table
)  : GroupItem{ stringId }
   , caption{ caption }
   , table{ table }
{
}

PopupSubMenu::~PopupSubMenu()
{}

auto PopupSubMenu::GetProperties() const -> Properties
{ return caption.empty() ? Extension : Whole; }

PopupMenuSection::~PopupMenuSection()
{}

PopupMenu::~PopupMenu() = default;

namespace {
struct PopupMenuImpl : PopupMenu, wxMenu
{
   PopupMenuImpl(void *pUserData_)
      : pUserData{ pUserData_ } {}

   ~PopupMenuImpl() override;

   void Popup( wxWindow &window, const wxPoint &pos ) override;

   void Extend(PopupMenuTable *pTable);

   void *pUserData;
};

class PopupMenuBuilder : public MenuRegistry::Visitor<PopupMenuTableTraits> {
public:
   PopupMenuBuilder(PopupMenuTable &table, PopupMenuImpl &menu, void *pUserData)
   : MenuRegistry::Visitor<PopupMenuTableTraits>{ std::tuple{
      [this](const PopupSubMenu &item, const auto &){
         if (!item.caption.empty()) {
            auto newMenu =
               std::make_unique<PopupMenuImpl>(mMenu->pUserData);
            mMenu = newMenu.get();
            mMenus.push_back(std::move(newMenu));
         }
      },

      [this](const PopupMenuTableEntry &entry, const auto &){
         switch (entry.type) {
            case PopupMenuTable::Entry::Item:
            {
               mMenu->Append(entry.id, entry.caption.Translation());
               break;
            }
            case PopupMenuTable::Entry::RadioItem:
            {
               mMenu->AppendRadioItem(entry.id, entry.caption.Translation());
               break;
            }
            case PopupMenuTable::Entry::CheckItem:
            {
               mMenu->AppendCheckItem(entry.id, entry.caption.Translation());
               break;
            }
            default:
               assert(false);
               break;
         }

         // This call is necessary for externally registered items, else
         // harmlessly redundant
         entry.handler.InitUserData(mpUserData);

         if (entry.init)
            entry.init(entry.handler, *mMenu, entry.id);

         mMenu->Bind(
            wxEVT_COMMAND_MENU_SELECTED, entry.func, &entry.handler, entry.id);
      },

      [this](const PopupSubMenu &item, const auto &){
         if (!item.caption.empty()) {
            auto subMenu = std::move(mMenus.back());
            mMenus.pop_back();
            mMenu = mMenus.empty() ? mRoot : mMenus.back().get();
            mMenu->AppendSubMenu(subMenu.release(), item.caption.Translation());
         }
      }},

      [this]() {
         mMenu->AppendSeparator();
      }
   }
   , mMenu{ &menu }
   , mRoot{ mMenu }
   , mpUserData{ pUserData }
   {}

   std::vector< std::unique_ptr<PopupMenuImpl> > mMenus;
   PopupMenuImpl *mMenu, *mRoot;
   void *const mpUserData;
};

PopupMenuImpl::~PopupMenuImpl()
{
   // Event connections between the parent window and the singleton table
   // object must be broken when this menu is destroyed.
   Disconnect();
}

void PopupMenuImpl::Popup( wxWindow &window, const wxPoint &pos )
{
   BasicMenu::Handle{ this }.Popup(
      wxWidgetsWindowPlacement{ &window }, { pos.x, pos.y }
   );
}
}

bool PopupMenu::PopupOwned(
   std::unique_ptr<PopupMenu> &menu, wxWindow &window, const wxPoint &pos,
   std::shared_ptr<void> keepAlive, std::function<void()> onFinished)
{
#ifdef AUDACITY_USE_ALICORN_SNAP_UI
   auto impl = dynamic_cast<PopupMenuImpl*>(menu.get());
   if (!impl)
      return false;
   menu.release();
   std::unique_ptr<wxMenu> owned{ static_cast<wxMenu*>(impl) };
   AlicornMenu::Options options;
   options.keepAlive = std::move(keepAlive);
   options.onFinished = std::move(onFinished);
   AlicornMenu::PopupOwned(window, std::move(owned), pos, std::move(options));
   return true;
#else
   (void)menu; (void)window; (void)pos; (void)keepAlive; (void)onFinished;
   return false;
#endif
}

void PopupMenuTable::ExtendMenu( PopupMenu &menu, PopupMenuTable &table )
{
   auto &theMenu = dynamic_cast<PopupMenuImpl&>(menu);

   PopupMenuBuilder visitor{ table, theMenu, theMenu.pUserData };
   Registry::VisitWithFunctions(visitor, table.Get(theMenu.pUserData).get(),
      table.GetRegistry(), table);
}

void PopupMenuTable::Append(
   const Identifier &stringId, PopupMenuTableEntry::Type type, int id,
   const TranslatableString &string, wxCommandEventFunction memFn,
   const PopupMenuTableEntry::InitFunction &init )
{
   Append( std::make_unique<Entry>(
      stringId, type, id, string, memFn, *this, init ) );
}

void PopupMenuTable::BeginSection( const Identifier &name )
{
   auto uSection = std::make_unique< PopupMenuSection >( name );
   auto section = uSection.get();
   mStack.back()->push_back( std::move( uSection ) );
   mStack.push_back( section );
}

void PopupMenuTable::EndSection()
{
   mStack.pop_back();
}

// static
std::unique_ptr< PopupMenu > PopupMenuTable::BuildMenu(
   PopupMenuTable *pTable, void *pUserData )
{
   // Rebuild as needed each time.  That makes it safe in case of language change.
   auto theMenu = std::make_unique<PopupMenuImpl>( pUserData );
   ExtendMenu( *theMenu, *pTable );
   return theMenu;
}

