#pragma once

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

@class GrpFile;

@interface GameListSource : NSObject <NSComboBoxDataSource>
{
    NSMutableArray *list;
}
- (id)init;
- (void)dealloc;
- (GrpFile *)grpAtIndex:(int)index;
- (int)findIndexForGrpname:(NSString *)grpname;
- (id)tableView:(NSTableView *)aTableView
    objectValueForTableColumn:(NSTableColumn *)aTableColumn
    row:(NSInteger)rowIndex;
- (int)numberOfRowsInTableView:(NSTableView *)aTableView;
@end
