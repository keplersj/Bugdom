//
// 3dmf.h
//

#include "qd3d_support.h"

extern	void Init3DMFManager(void);
extern	void LoadGrouped3DMF(FSSpec *spec, Byte groupNum);
extern	void Free3DMFGroup(Byte groupNum);
extern	void DeleteAll3DMFGroups(void);

