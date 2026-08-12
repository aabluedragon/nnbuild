#pragma once

#import <Foundation/Foundation.h>

@interface GrpFile : NSObject
{
    NSString *namestring;
    NSString *grpnamestring;
    grpfile_t const *fg;
}
- (id)initWithGrpfile:(grpfile_t const *)grpfile;
- (void)dealloc;
- (NSString *)name;
- (NSString *)grpname;
- (grpfile_t const *)entryptr;
@end
