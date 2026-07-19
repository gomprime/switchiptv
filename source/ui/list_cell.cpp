#include "ui/list_cell.hpp"

ListCell::ListCell() {
    this->inflateFromXMLRes("xml/cells/list_cell.xml");
}

brls::RecyclerCell* ListCell::create() {
    return new ListCell();
}
