// Author: Alexander Thomson <thomson@cs.yale.edu>
//
// A store is simply something that runs Actions.

#ifndef CALVIN_COMPONENTS_STORE_STORE_H_
#define CALVIN_COMPONENTS_STORE_STORE_H_

#include "common/types.h"

class Action;  // (lawsuit)

class Store {
 public:
  virtual ~Store() {}
  virtual void GetRWSets(Action* action) = 0;
  virtual void Run(Action* action) = 0;
  virtual bool IsLocal(const string& path) = 0;
};

#endif  // CALVIN_COMPONENTS_STORE_STORE_H_

