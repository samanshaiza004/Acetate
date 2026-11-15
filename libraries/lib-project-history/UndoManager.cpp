/**********************************************************************

  Audacity: A Digital Audio Editor

  UndoManager.cpp

  Dominic Mazzoni

*******************************************************************//**

\class UndoManager
\brief Works with HistoryDialog to provide the Undo functionality.

*//****************************************************************//**

\class UndoStackElem
\brief Holds one item with description and time range for the
UndoManager

*//*******************************************************************/



#include "UndoManager.h"

#include <wx/hashset.h>

#include "BasicUI.h"
#include "Project.h"
#include "TransactionScope.h"
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
#include "OdinApplicationBackend.h"
#endif
//#include "NoteTrack.h"  // for Sonify* function declarations


UndoStateExtension::~UndoStateExtension() = default;

bool UndoStateExtension::CanUndoOrRedo(const AudacityProject &)
{
   return true;
}

namespace {
   using Savers = std::vector<UndoRedoExtensionRegistry::Saver>;
   static Savers &GetSavers()
   {
      static Savers theSavers;
      return theSavers;
   }

   UndoState::Extensions GetExtensions(AudacityProject &project)
   {
      UndoState::Extensions result;
      for (auto &saver : GetSavers())
         if (saver)
            result.emplace_back(saver(project));
      return result;
   }
}

UndoRedoExtensionRegistry::Entry::Entry(const Saver &saver)
{
   GetSavers().emplace_back(saver);
}

using SampleBlockID = long long;

static const AudacityProject::AttachedObjects::RegisteredFactory key{
   [](AudacityProject &project)
      { return std::make_unique<UndoManager>( project ); }
};

UndoManager &UndoManager::Get( AudacityProject &project )
{
   return project.AttachedObjects::Get< UndoManager >( key );
}

const UndoManager &UndoManager::Get( const AudacityProject &project )
{
   return Get( const_cast< AudacityProject & >( project ) );
}

UndoManager::UndoManager( AudacityProject &project )
   : mProject{ project }
{
   current = -1;
   saved = -1;
}

UndoManager::~UndoManager()
{
   wxASSERT( stack.empty() );
}

void UndoManager::GetShortDescription(unsigned int n, TranslatableString *desc)
{
   wxASSERT(n < stack.size());

   *desc = stack[n]->shortDescription;
}

void UndoManager::SetLongDescription(
  unsigned int n, const TranslatableString &desc)
{
   n -= 1;

   wxASSERT(n < stack.size());

   stack[n]->description = desc;
}

void UndoManager::RemoveStateAt(int n)
{
   // Remove the state from the array first, and destroy it at function exit.
   // Because in case of callbacks from destruction of Sample blocks, there
   // might be a yield to GUI and other events might inspect the undo stack
   // (such as history window update).  Don't expose an inconsistent stack
   // state.
   auto iter = stack.begin() + n;
   auto state = std::move(*iter);
   stack.erase(iter);
}

void UndoManager::EnqueueMessage(UndoRedoMessage message)
{
   BasicUI::CallAfter([wThis = weak_from_this(), message]{
      if (auto pThis = wThis.lock())
         pThis->Publish(message);
   });
}

void UndoManager::RemoveStates(size_t begin, size_t end)
{
   Publish({ UndoRedoMessage::BeginPurge, begin, end });
   auto cleanup =
      finally([&]{ Publish({ UndoRedoMessage::EndPurge }); });

   // Wrap the whole in a savepoint for better performance
   TransactionScope trans{mProject, "DiscardingUndoStates"};

   for (size_t ii = begin; ii < end; ++ii) {
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
      audacity_odin_undo_history_remove_one(
         &current, &saved, static_cast<uint64_t>(begin));
#else
      if (current > begin)
        --current;
      if (saved > static_cast<int>(begin))
        --saved;
#endif

      RemoveStateAt(begin);
   }

   // Success, commit the savepoint
   trans.Commit();

   if (begin != end)
      EnqueueMessage({ UndoRedoMessage::Purge });
}

void UndoManager::ClearStates()
{
   RemoveStates(0, stack.size());
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_reset(&current, &saved);
#else
   current = -1;
   saved = -1;
#endif
}

unsigned int UndoManager::GetNumStates()
{
   return stack.size();
}

unsigned int UndoManager::GetCurrentState()
{
   return current;
}

void UndoManager::StopConsolidating()
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_stop_consolidating(&mayConsolidate);
#else
   mayConsolidate = 0;
#endif
}

bool UndoManager::UndoAvailable()
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   int32_t target = 0;
   return audacity_odin_undo_history_target(
      current, static_cast<uint64_t>(stack.size()), -1, &target) != 0 &&
      CheckAvailable(target);
#else
   return CheckAvailable(current - 1);
#endif
}

bool UndoManager::RedoAvailable()
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   int32_t target = 0;
   return audacity_odin_undo_history_target(
      current, static_cast<uint64_t>(stack.size()), 1, &target) != 0 &&
      CheckAvailable(target);
#else
   return CheckAvailable(current + 1);
#endif
}

bool UndoManager::CheckAvailable(int index)
{
   if (index < 0 || index >= (int)stack.size())
      return false;
   auto &extensions = stack[index]->state.extensions;
   return std::all_of(extensions.begin(), extensions.end(), [&](auto &ext){
      return !ext || ext->CanUndoOrRedo(mProject);
   });
}

void UndoManager::ModifyState()
{
   if (current == wxNOT_FOUND) {
      return;
   }

//   SonifyBeginModifyState();
   auto &state = stack[current]->state;

   // Re-create all captured project state
   state.extensions = GetExtensions(mProject);

//   SonifyEndModifyState();

   EnqueueMessage({ UndoRedoMessage::Modified });
}

void UndoManager::RenameState( int state,
   const TranslatableString &longDescription,
   const TranslatableString &shortDescription)
{
   if (state >= 0 && state < stack.size() ) {
      auto &theState = *stack[state];
      theState.description = longDescription;
      theState.shortDescription = shortDescription;

      EnqueueMessage({ UndoRedoMessage::Renamed });
   }
}

void UndoManager::PushState(const TranslatableString &longDescription,
                            const TranslatableString &shortDescription,
                            UndoPush flags)
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   const auto shouldConsolidate = audacity_odin_undo_history_prepare_push(
      &mayConsolidate,
      (flags & UndoPush::CONSOLIDATE) != UndoPush::NONE,
      // Compare full translations, not message IDs.
      lastAction.Translation() == longDescription.Translation());
#else
   const auto shouldConsolidate =
      (flags & UndoPush::CONSOLIDATE) != UndoPush::NONE &&
      // Compare full translations, not message IDs.
      lastAction.Translation() == longDescription.Translation() &&
      mayConsolidate;
#endif
   if (shouldConsolidate) {
      ModifyState();
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
      audacity_odin_undo_history_finish_consolidated_push(current, &saved);
#else
      // MB: If the "saved" state was modified by ModifyState, reset
      //  it so that UnsavedChanges returns true.
      if (current == saved) {
         saved = -1;
      }
#endif
      return;
   }

#ifndef AUDACITY_USE_ODIN_CALIBER_BACKEND
   mayConsolidate = true;
#endif

   AbandonRedo();

   stack.push_back(
      std::make_unique<UndoStackElem>
         (GetExtensions(mProject), longDescription, shortDescription)
   );

#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_commit_push(&current);
#else
   current++;
#endif

   lastAction = longDescription;

   EnqueueMessage({ UndoRedoMessage::Pushed });
}

void UndoManager::AbandonRedo()
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_abandon_redo(current, &saved);
#else
   if (saved > current) {
      saved = -1;
   }
#endif
   RemoveStates( current + 1, stack.size() );
}

void UndoManager::SetStateTo(unsigned int n, const Consumer &consumer)
{
   wxASSERT(n < stack.size());

#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_set_current(&current, &mayConsolidate, n);
#else
   current = n;
   mayConsolidate = false;
#endif

   lastAction = {};
#ifndef AUDACITY_USE_ODIN_CALIBER_BACKEND
   mayConsolidate = false;
#endif

   consumer( *stack[current] );

   EnqueueMessage({ UndoRedoMessage::Reset });
}

void UndoManager::Undo(const Consumer &consumer)
{
   wxASSERT(UndoAvailable());

#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_move(&current, &mayConsolidate, -1);
#else
   current--;
   mayConsolidate = false;
#endif

   lastAction = {};
#ifndef AUDACITY_USE_ODIN_CALIBER_BACKEND
   mayConsolidate = false;
#endif

   consumer( *stack[current] );

   EnqueueMessage({ UndoRedoMessage::UndoOrRedo });
}

void UndoManager::Redo(const Consumer &consumer)
{
   wxASSERT(RedoAvailable());

#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_move(&current, &mayConsolidate, 1);
#else
   current++;
   mayConsolidate = false;
#endif

   /*
   if (!RedoAvailable()) {
      *sel0 = stack[current]->sel0;
      *sel1 = stack[current]->sel1;
   }
   else {
      current++;
      *sel0 = stack[current]->sel0;
      *sel1 = stack[current]->sel1;
      current--;
   }
   */

   lastAction = {};
#ifndef AUDACITY_USE_ODIN_CALIBER_BACKEND
   mayConsolidate = false;
#endif

   consumer( *stack[current] );

   EnqueueMessage({ UndoRedoMessage::UndoOrRedo });
}

void UndoManager::VisitStates( const Consumer &consumer, bool newestFirst )
{
   auto fn = [&]( decltype(stack[0]) &ptr ){ consumer( *ptr ); };
   if (newestFirst)
      std::for_each(stack.rbegin(), stack.rend(), fn);
   else
      std::for_each(stack.begin(), stack.end(), fn);
}

void UndoManager::VisitStates(
   const Consumer &consumer, size_t begin, size_t end )
{
   auto size = stack.size();
   if (begin < end) {
      end = std::min(end, size);
      for (auto ii = begin; ii < end; ++ii)
         consumer(*stack[ii]);
   }
   else {
      if (size == 0)
         return;
      begin = std::min(begin, size - 1);
      for (auto ii = begin; ii > end; --ii)
         consumer(*stack[ii]);
   }
}

void UndoManager::MarkUnsaved()
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_mark_unsaved(&saved);
#else
   saved = -1;
#endif
}

bool UndoManager::UnsavedChanges() const
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   return audacity_odin_undo_history_has_unsaved_changes(current, saved) != 0;
#else
   return (saved != current);
#endif
}

void UndoManager::StateSaved()
{
#ifdef AUDACITY_USE_ODIN_CALIBER_BACKEND
   audacity_odin_undo_history_mark_saved(current, &saved);
#else
   saved = current;
#endif
}

int UndoManager::GetSavedState() const
{
   return saved;
}

// currently unused
//void UndoManager::Debug()
//{
//   for (unsigned int i = 0; i < stack.size(); i++) {
//      for (auto t : stack[i]->tracks->Any())
//         wxPrintf(wxT("*%d* %s %f\n"),
//                  i, (i == (unsigned int)current) ? wxT("-->") : wxT("   "),
//                t ? t->GetEndTime()-t->GetStartTime() : 0);
//   }
//}
