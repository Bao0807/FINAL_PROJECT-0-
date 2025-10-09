#pragma once
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

#include "Queue.h"
#include "Menu.h"
#include "Table.h"
#include "bill.h"
#include "Revenue.h"
#include "List.h"

using namespace std;

// ======= Global lists for FIFO pipeline =======
const int MAX_ORDERS = 1000;
IntList ListPending = IntList();                 // IDs đơn ở trạng thái Pending
IntList ListDone = IntList();                 // IDs đơn đã Done (chờ thanh toán)
extern Order   allOrders[MAX_ORDERS];       // Bản sao đơn theo ID

// ===== timer globals =====
thread g_timerThread;
atomic<bool> g_timerRunning(false);
Queue *g_queuePtr = nullptr;

// ===== Helpers =====
static inline bool hasCooking(Queue &q)
{
    for (int i = 0; i <= q.right; i++)
    {
        if (q.orders[i].status == "Cooking") return true;
    }
    return false;
}

static inline void printOrderRow(const Order &o)
{
    cout << "OrderID: " << o.id
         << " | Name: " << o.customerName
         << " | Table: " << o.tableNumber
         << " | Status: " << o.status
         << " | Total: " << formatPrice(o.total)
         << " | Remain: " << o.totalRemainingTime << "s\n";
}

// ===== Forward Decls =====
string formatPrice(long long price); // Menu.h đã có
int getLastOrderID();

Order *findOrderByID(Queue &q, int id);
bool removeOrderByID(Queue &q, int id, Order &out);

void advanceTime(Queue &q, int seconds);
void startTimer(Queue *qp);

void addOrder(Queue &q, int &idCounter){
    clearScreen();
    Order o;
    o.id = ++idCounter;

    cout << "Ten khach hang: ";
    cin.ignore();
    getline(cin, o.customerName);

    displayTables();
    int tableNum = chooseTable();
    if (tableNum == 0) return;
    o.tableNumber = tableNum;
    gTableStatus[o.tableNumber - 1] = "Full";
    gTableOwner[o.tableNumber - 1] = o.id;

    clearScreen();
    char more = 'y';
    do
    {
        if (o.itemCount >= MAX_ITEMS)
        {
            cout << "Da dat so luong toi da!\n";
            break;
        }

        displayMenu();
        cout << "Chon mon an (theo ten hoac STT): ";
        string inputFood;
        getline(cin, inputFood);
        MenuItem sel = getMenuItem(inputFood);

        if (!sel.available || sel.price == 0)
        {
            cout << "Lua chon khong hop le hoac da het hang.\n";
            enter();
            continue;
        }

        OrderDetail d;
        d.foodName = sel.foodName;
        cout << "So luong: ";
        if (!(cin >> d.quantity) || d.quantity <= 0){
            cin.clear();
            cin.ignore();
            cout << "So luong khong hop le.\n";
            continue;
        }
        cin.ignore();

        d.price = sel.price;
        d.subtotal = d.price * d.quantity;
        d.prepTime = sel.prepTime;
        d.remainingTime = sel.prepTime * d.quantity;

        o.items[o.itemCount++] = d;
        o.total += d.subtotal;

        cout << "Them mot mon khac? (y/n): ";
        cin >> more;
        cin.ignore();
    } while (more == 'y' || more == 'Y');

    o.totalRemainingTime = 0;
    for (int j = 0; j < o.itemCount; j++)
        o.totalRemainingTime += o.items[j].remainingTime;

    // Đơn mới -> Pending
    o.status = "Pending";
    allOrders[o.id] = o;
    ListPending.add(o.id);

    cout << "\x1b[32mDa luu ID don hang." << o.id << " (Dang cho xu li)\x1b[0m\n";
    cout << "In hoa don tam thoi (bat dau lam mon)? (y/n): ";
    char choice;
    cin >> choice;

    if (choice == 'y' || choice == 'Y'){
        Order &ref = allOrders[o.id];
        ref.status = hasCooking(q) ? "Wait" : "Cooking";

        if (!enqueue(q, ref)){
            cout << "Hang doi da day! Hoa don giu nguyen trang thai cho xu li.\n";
        }
        else
        {
            // Bỏ khỏi Pending
            for (int i = 0; i < ListPending.getSize(); i++)
            {
                if (ListPending.getValue(i) == ref.id)
                {
                    ListPending.removeAt(i);
                    break;
                }
            }
            printBill(ref);
            cout << "[Temp bill] -> " << ref.status << "\n";
            startTimer(&q);
        }
    } else{
        cout << "Da luu don hang (trang thai cho nau).\n";
    }

    enter();
}

void displayQueue(Queue &q)
{
    while (true)
    {   
        displayTables();
        cout << "\t\t\t\t\t==== DON HANG CHUA XU LI ====\n";
        if (ListPending.getSize() == 0)
            cout << "Khong co don chua xu li.\n";
        else
            for (int i = 0; i < ListPending.getSize(); i++)
                printOrderRow(allOrders[ListPending.getValue(i)]);

        cout << "\n\t\t\t\t\t==== DON HANG DANG XU LI ====\n";
        if (q.right < 0)
            cout << "Khong co don dang xu li.\n";
        else
            for (int i = 0; i <= q.right; i++)
                printOrderRow(q.orders[i]);

        cout << "\n\t\t\t\t\t=== DON HANG DA HOAN THANH ===\n";
        if (ListDone.getSize() == 0)
            cout << "Khong co don da hoan thanh.\n";
        else
            for (int i = 0; i < ListDone.getSize(); i++)
                printOrderRow(allOrders[ListDone.getValue(i)]);

        cout << "\nCac lua chon: 1 - Xoa | 2 - Sua | 3 - Hoa don tam thoi | 4 - Lam moi | 0 - Quay lai\nLua chon: ";
        int act;
        if (!(cin >> act)){
            cin.clear();
            cin.ignore();
            continue;
        }
        cin.ignore();

        if (act == 1)
        {
            // Xoá: chỉ cho Pending hoặc đơn trong Queue nhưng KHÔNG Cooking/Done
            int id;
            cout << "ID don hang can xoa: ";
            cin >> id;
            cin.ignore();

            Order *f = findOrderByID(q, id);
            if (!f)
            {
                // thử Pending
                int pendIdx = -1;
                for (int i = 0; i < ListPending.getSize(); i++)
                    if (ListPending.getValue(i) == id) { pendIdx = i; break; }

                if (pendIdx != -1)
                {
                    int t = allOrders[id].tableNumber;
                    ListPending.removeAt(pendIdx);
                    gTableStatus[t - 1] = "Empty";
                    gTableOwner[t - 1] = 0;
                    cout << "Da xoa don hang.\n";
                }
                else
                    cout << "Khong tim thay don hang.\n";
            }
            else
            {
                if (f->status == "Cooking" || f->status == "Done")
                {
                    cout << "Khong the xoa don hang dang nau hoac da hoan thanh.\n";
                }
                else
                {
                    Order rem;
                    if (removeOrderByID(q, id, rem))
                    {
                        int t = rem.tableNumber;
                        if (!anyOrderForTable(q, t))
                            gTableStatus[t - 1] = "Empty";
                        cout << "Da xoa.\n";
                    }
                    else cout << "Khong tim thay don hang.\n";
                }
            }
        }
        else if (act == 2)
        {
            // Edit: cho Queue hoặc Pending (không Cooking/Done)
            int id;
            cout << "ID don hang can sua: ";
            cin >> id;

            Order *f = findOrderByID(q, id);
            if (!f)
            {
                for (int i = 0; i < ListPending.getSize(); i++)
                    if (ListPending.getValue(i) == id) { f = &allOrders[id]; break; }
                if (!f)
                {
                    cout << "Khong tim thay don hang.\n";
                    continue;
                }
            }

            cout << "Ten hien tai: " << f->customerName << "\n";
            cout << "Nhap ten moi (hoac '.' de giu nguyen): ";
            string name;
            cin.ignore();
            getline(cin, name);
            if (name != ".") f->customerName = name;

            if (f->status == "Cooking" || f->status == "Done"){
                cout << "Khong the them mon khi dang nau hoac da xong.\n";
                continue;
            }

            cout << "Them mon moi? (y/n): ";
            char c; cin >> c;
            if (c == 'y' || c == 'Y')
            {
                cin.ignore();
                char more = 'y';
                do
                {
                    displayMenu();
                    cout << "Nhap ten mon hoac so: ";
                    string food; getline(cin, food);
                    MenuItem sel = getMenuItem(food);
                    if (!sel.available || sel.price == 0){
                        cout << "Mon khong hop le.\n";
                        cout << "Them tiep? (y/n): ";
                        cin >> more; cin.ignore();
                        continue;
                    }

                    OrderDetail d;
                    d.foodName = sel.foodName;
                    cout << "So luong: ";
                    if (!(cin >> d.quantity) || d.quantity <= 0) {
                        cin.clear(); cin.ignore();
                        cout << "So luong khong hop le.\n";
                        cout << "Them tiep? (y/n): ";
                        cin >> more; cin.ignore();
                        continue;
                    }
                    cin.ignore();

                    d.price = sel.price;
                    d.subtotal = d.price * d.quantity;
                    d.prepTime = sel.prepTime;
                    d.remainingTime = sel.prepTime * d.quantity;

                    f->items[f->itemCount++] = d;
                    f->total += d.subtotal;

                    cout << "Them tiep? (y/n): ";
                    cin >> more; cin.ignore();
                } while (more == 'y' || more == 'Y');

                f->totalRemainingTime = 0;
                for (int j = 0; j < f->itemCount; ++j)
                    f->totalRemainingTime += f->items[j].remainingTime;
            }

            cout << "Da cap nhat don hang.\n";
        }
        else if (act == 3)
        {
            // Temp bill: chuyển Pending -> Queue (Cooking/Wait)
            cout << "ID don hang can xuat hoa don tam thoi: ";
            int id;
            cin >> id;
            cin.ignore();

            Order *f = findOrderByID(q, id);
            if (!f)
            {
                bool foundPending = false;
                for (int i = 0; i < ListPending.getSize(); i++)
                    if (ListPending.getValue(i) == id) { foundPending = true; break; }

                if (foundPending)
                {
                    Order &pend = allOrders[id];
                    pend.status = hasCooking(q) ? "Wait" : "Cooking";
                    if (!enqueue(q, pend))
                        cout << "Hang doi da day!\n";
                    else
                    {
                        for (int i = 0; i < ListPending.getSize(); i++)
                            if (ListPending.getValue(i) == id) { ListPending.removeAt(i); break; }
                        printBill(pend);
                        cout << "[Temp bill] -> " << pend.status << "\n";
                        startTimer(&q);
                    }
                }
                else cout << "Khong tim thay don hang.\n";
            }
            else{
                f->status = hasCooking(q) ? "Wait" : "Cooking";
                printBill(*f);
                cout << "[Temp bill] -> " << f->status << "\n";
                startTimer(&q);
            }
            enter();
        }
        else if (act == 4)
        {
            clearScreen();
            continue;
        }
        else if (act == 0) break;
    }
}


void searchCustomer(Queue &q)
{
    if (q.right < 0 && ListPending.getSize() == 0 && ListDone.getSize() == 0)
    {
        cout << "Khong co don nao.\n";
        return;
    }

    cout << "Nhap ten khach hang: ";
    string key;
    getline(cin, key);
    bool found = false;

    for (int i = 0; i <= q.right; i++)
    {
        Order &o = q.orders[i];
        if (containsNaive(o.customerName, key)) { printOrderRow(o); found = true; }
    }
    for (int i = 0; i < ListPending.getSize(); i++)
    {
        Order &o = allOrders[ListPending.getValue(i)];
        if (containsNaive(o.customerName, key)) { printOrderRow(o); found = true; }
    }
    for (int i = 0; i < ListDone.getSize(); i++)
    {
        Order &o = allOrders[ListDone.getValue(i)];
        if (containsNaive(o.customerName, key)) { printOrderRow(o); found = true; }
    }

    if (!found) cout << "Khong tim thay.\n";
}


void advanceTime(Queue &q, int seconds)
{
    if (seconds <= 0 || q.right < 0) return;

    // tìm đơn đầu tiên đang Cooking
    int targetOffset = -1;
    for (int i = 0; i <= q.right; i++)
    {
        if (q.orders[i].status == "Cooking") { targetOffset = i; break; }
    }
    if (targetOffset == -1) return;

    int idxT = targetOffset;
    Order &o = q.orders[idxT];

    if (o.totalRemainingTime > 0)
    {
        o.totalRemainingTime -= seconds;
        if (o.totalRemainingTime < 0) o.totalRemainingTime = 0;
    }

    if (o.totalRemainingTime == 0 && o.status == "Cooking")
    {
        // đánh dấu Done, loại khỏi Queue (FIFO), đưa vào ListDone
        o.status = "Done";
        Order doneOrder = o;

        // Dịch trái xoá phần tử targetOffset
        for (int k = targetOffset; k < q.right; ++k)
        {
            int from = k + 1;
            int to   = k;
            q.orders[to] = q.orders[from];
        }
        q.right--;

        allOrders[doneOrder.id] = doneOrder;
        ListDone.add(doneOrder.id);

        // Promote đơn Wait đầu tiên (nếu có)
        for (int j = 0; j <= q.right; ++j)
        {
            int jdx = j;
            if (q.orders[jdx].status == "Wait")
            {
                q.orders[jdx].status = "Cooking";
                break;
            }
        }
    }
}

void startTimer(Queue *qp)
{
    g_queuePtr = qp;
    if (g_timerRunning.load()) return;

    g_timerRunning.store(true);
    g_timerThread = thread([](){
        while (g_timerRunning.load())
        {
            if (g_queuePtr) advanceTime(*g_queuePtr, 1);
            this_thread::sleep_for(chrono::seconds(1));
        }
    });
    g_timerThread.detach(); 
}


Order *findOrderByID(Queue &q, int id)
{
    for (int i = 0; i <= q.right; i++)
    {
        if (q.orders[i].id == id) return &q.orders[i];
    }
    return nullptr;
}

int getLastOrderID()
{
    int mx = 0;
    // Queue
    if (g_queuePtr)
    {
        Queue &q = *g_queuePtr;
        for (int i = 0; i <= q.right; i++)
            mx = max(mx, q.orders[i].id);
    }
    // Pending
    for (int i = 0; i < ListPending.getSize(); i++)
        mx = max(mx, allOrders[ListPending.getValue(i)].id);
    // Done
    for (int i = 0; i < ListDone.getSize(); i++)
        mx = max(mx, allOrders[ListDone.getValue(i)].id);
    return mx;
}
