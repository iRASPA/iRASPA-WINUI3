/********************************************************************************************************************
    Undo/redo stack, modelled on the NSUndoManager behaviour iRASPA-COCOA relies
    on: an operation registers the closure that reverses it, and running that
    closure during an undo registers its own inverse, which becomes the redo
    entry. Cocoa's registerUndo(withTarget:handler:) + setActionName(_:) pair is
    folded into a single registerUndo(name, action) call here.
 ********************************************************************************************************************/

#pragma once

#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class RKUndoStack
{
public:
  using Action = std::function<void()>;

  // Registering while an undo is running builds the redo entry, and vice
  // versa; a fresh registration invalidates the redo history.
  void registerUndo(std::wstring name, Action action)
  {
    if (!action)
      return;
    if (_groupDepth > 0)
    {
      if (_groupName.empty())
        _groupName = std::move(name);
      _groupActions.push_back(std::move(action));
      return;
    }
    Entry entry;
    entry.name = std::move(name);
    entry.actions.push_back(std::move(action));
    push(std::move(entry));
  }

  // Several registrations that undo as one step (Cocoa's begin/endUndoGrouping
  // around a multi-row delete).
  void beginUndoGrouping(std::wstring name = {})
  {
    if (_groupDepth++ == 0)
    {
      _groupName = std::move(name);
      _groupActions.clear();
    }
  }

  void endUndoGrouping()
  {
    if (_groupDepth == 0 || --_groupDepth > 0)
      return;
    if (_groupActions.empty())
    {
      _groupName.clear();
      return;
    }
    Entry entry;
    entry.name = std::move(_groupName);
    entry.actions = std::move(_groupActions);
    _groupActions.clear();
    _groupName.clear();
    push(std::move(entry));
  }

  bool isUndoing() const { return _isUndoing; }
  bool isRedoing() const { return _isRedoing; }
  bool canUndo() const { return !_undo.empty(); }
  bool canRedo() const { return !_redo.empty(); }
  std::wstring undoActionName() const { return _undo.empty() ? std::wstring{} : _undo.back().name; }
  std::wstring redoActionName() const { return _redo.empty() ? std::wstring{} : _redo.back().name; }

  void undo()
  {
    if (_undo.empty() || _groupDepth > 0 || _isUndoing || _isRedoing)
      return;
    Entry entry = std::move(_undo.back());
    _undo.pop_back();
    _isUndoing = true;
    run(entry);
    _isUndoing = false;
  }

  void redo()
  {
    if (_redo.empty() || _groupDepth > 0 || _isUndoing || _isRedoing)
      return;
    Entry entry = std::move(_redo.back());
    _redo.pop_back();
    _isRedoing = true;
    run(entry);
    _isRedoing = false;
  }

  void clear()
  {
    _undo.clear();
    _redo.clear();
    _groupActions.clear();
    _groupName.clear();
    _groupDepth = 0;
  }

private:
  struct Entry
  {
    std::wstring name;
    std::vector<Action> actions;
  };

  // The inverse registrations made while the entry runs are collected into a
  // single opposite-side entry, so a grouped undo redoes as one step too.
  void run(Entry& entry)
  {
    beginUndoGrouping(entry.name);
    for (auto it = entry.actions.rbegin(); it != entry.actions.rend(); ++it)
      (*it)();
    endUndoGrouping();
  }

  void push(Entry&& entry)
  {
    if (_isUndoing)
    {
      _redo.push_back(std::move(entry));
    }
    else
    {
      if (!_isRedoing)
        _redo.clear();
      _undo.push_back(std::move(entry));
      while (_undo.size() > kMaxDepth)
        _undo.pop_front();
    }
  }

  static constexpr size_t kMaxDepth = 128;

  std::deque<Entry> _undo;
  std::deque<Entry> _redo;
  std::vector<Action> _groupActions;
  std::wstring _groupName;
  int _groupDepth{0};
  bool _isUndoing{false};
  bool _isRedoing{false};
};
