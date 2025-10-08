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
extern IntList ListPending;                 // IDs đơn ở trạng thái Pending
extern IntList ListDone;                    // IDs đơn đã Done (chờ thanh toán)
extern Order   allOrders[MAX_ORDERS];       // Bản sao đơn theo ID

// ===== timer globals =====
thread g_timerThread;
atomic<bool> g_timerRunning(false);
Queue *g_queuePtr = nullptr;

// ===== Helpers =====
static inline bool hasCooking(Queue &q)
{
    for (int i = 0; i < q.count; ++i)
    {
        int idx = (q.front + i) % MAX;
        if (q.orders[idx].status == "Cooking") return true;
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

// ===================================================================
// Add Order: tạo đơn -> vào ListPending (Pending). Chỉ khi in bill tạm
// mới chuyển sang Cooking/Wait và enqueue vào Queue.
// ===================================================================
void addOrder(Queue &q, int &idCounter)
{
    clearScreen();
    Order o;
    o.id = ++idCounter;

    cout << "Customer name: ";
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    getline(cin, o.customerName);

    int tableNum = chooseTable();
    o.tableNumber = tableNum;
    gTableStatus[o.tableNumber - 1] = "Full";
    gTableOwner[o.tableNumber - 1] = o.id;

    clearScreen();
    char more = 'n';
    do
    {
        if (o.itemCount >= MAX_ITEMS)
        {
            cout << "Reached max items!\n";
            break;
        }

        displayMenu();
        cin.ignore();
        cout << "Choose food (number or exact name): ";
        string inputFood;
        getline(cin, inputFood);
        MenuItem sel = getMenuItem(inputFood);

        if (!sel.available || sel.price == 0)
        {
            cout << "Invalid choice or sold out.\n";
            enter();
            continue;
        }

        OrderDetail d;
        d.foodName = sel.foodName;
        cout << "Quantity: ";
        if (!(cin >> d.quantity) || d.quantity <= 0)
        {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            cout << "Invalid quantity.\n";
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        d.price = sel.price;
        d.subtotal = d.price * d.quantity;
        d.prepTime = sel.prepTime;
        d.remainingTime = sel.prepTime * d.quantity;

        o.items[o.itemCount++] = d;
        o.total += d.subtotal;

        cout << "Add more food? (y/n): ";
        cin >> more;
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    } while (more == 'y' || more == 'Y');

    o.totalRemainingTime = 0;
    for (int j = 0; j < o.itemCount; j++)
        o.totalRemainingTime += o.items[j].remainingTime;

    // Đơn mới -> Pending
    o.status = "Pending";
    allOrders[o.id] = o;
    ListPending.add(o.id);

    cout << "\x1b[32mSaved order ID " << o.id << " (Pending)\x1b[0m\n";
    cout << "Print temporary bill (start cooking)? (y/n): ";
    char choice;
    cin >> choice;
    cin.ignore(numeric_limits<streamsize>::max(), '\n');

    if (choice == 'y' || choice == 'Y')
    {
        Order &ref = allOrders[o.id];
        ref.status = hasCooking(q) ? "Wait" : "Cooking";

        if (!enqueue(q, ref))
        {
            cout << "Queue full! Order remains pending.\n";
        }
        else
        {
            // Bỏ khỏi Pending
            for (int i = 0; i < ListPending.getSize(); ++i)
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
    }
    else
    {
        cout << "Order saved as Pending.\n";
    }

    enter();
}

// ===================================================================
// Display Queue: show Pending, Queue, Done + thao tác
// ===================================================================
void displayQueue(Queue &q)
{
    while (true)
    {   
        displayTables();
        cout << "\n===== PENDING ORDERS =====\n";
        if (ListPending.getSize() == 0)
            cout << "No pending orders.\n";
        else
            for (int i = 0; i < ListPending.getSize(); ++i)
                printOrderRow(allOrders[ListPending.getValue(i)]);

        cout << "\n===== ORDERS IN QUEUE =====\n";
        if (q.count == 0)
            cout << "No orders in queue.\n";
        else
            for (int i = 0; i < q.count; i++)
                printOrderRow(q.orders[(q.front + i) % MAX]);

        cout << "\n===== DONE ORDERS =====\n";
        if (ListDone.getSize() == 0)
            cout << "No done orders.\n";
        else
            for (int i = 0; i < ListDone.getSize(); ++i)
                printOrderRow(allOrders[ListDone.getValue(i)]);

        cout << "\nOptions: 1-Delete  2-Edit  3-Temp bill  4-Refresh  0-Back\nChoice: ";
        int act;
        if (!(cin >> act))
        {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }
        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        if (act == 1)
        {
            // Xoá: chỉ cho Pending hoặc đơn trong Queue nhưng KHÔNG Cooking/Done
            int id;
            cout << "Enter ID to delete: ";
            cin >> id;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');

            Order *f = findOrderByID(q, id);
            if (!f)
            {
                // thử Pending
                int pendIdx = -1;
                for (int i = 0; i < ListPending.getSize(); ++i)
                    if (ListPending.getValue(i) == id) { pendIdx = i; break; }

                if (pendIdx != -1)
                {
                    int t = allOrders[id].tableNumber;
                    ListPending.removeAt(pendIdx);
                    gTableStatus[t - 1] = "Empty";
                    gTableOwner[t - 1] = 0;
                    cout << "Deleted pending order.\n";
                }
                else
                    cout << "Not found.\n";
            }
            else
            {
                if (f->status == "Cooking" || f->status == "Done")
                {
                    cout << "Cannot delete while cooking or done.\n";
                }
                else
                {
                    Order rem;
                    if (removeOrderByID(q, id, rem))
                    {
                        int t = rem.tableNumber;
                        if (!anyOrderForTable(q, t))
                            gTableStatus[t - 1] = "Empty";
                        cout << "Deleted.\n";
                    }
                    else cout << "Not found.\n";
                }
            }
        }
        else if (act == 2)
        {
            // Edit: cho Queue hoặc Pending (không Cooking/Done)
            int id;
            cout << "Nhap ID can sua: ";
            cin >> id;

            Order *f = findOrderByID(q, id);
            if (!f)
            {
                for (int i = 0; i < ListPending.getSize(); ++i)
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
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            getline(cin, name);
            if (name != ".") f->customerName = name;

            if (f->status == "Cooking" || f->status == "Done")
            {
                cout << "Khong the them mon khi dang nau hoac da xong.\n";
                continue;
            }

            cout << "Them mon moi? (y/n): ";
            char c; cin >> c;
            if (c == 'y' || c == 'Y')
            {
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                char more = 'y';
                do
                {
                    displayMenu();
                    cout << "Nhap ten mon hoac so: ";
                    string food; getline(cin, food);
                    MenuItem sel = getMenuItem(food);
                    if (!sel.available || sel.price == 0)
                    {
                        cout << "Mon khong hop le.\n";
                        cout << "Them tiep? (y/n): ";
                        cin >> more; cin.ignore(numeric_limits<streamsize>::max(), '\n');
                        continue;
                    }

                    OrderDetail d;
                    d.foodName = sel.foodName;
                    cout << "So luong: ";
                    if (!(cin >> d.quantity) || d.quantity <= 0) {
                        cin.clear(); cin.ignore(numeric_limits<streamsize>::max(), '\n');
                        cout << "So luong khong hop le.\n";
                        cout << "Them tiep? (y/n): ";
                        cin >> more; cin.ignore(numeric_limits<streamsize>::max(), '\n');
                        continue;
                    }
                    cin.ignore(numeric_limits<streamsize>::max(), '\n');

                    d.price = sel.price;
                    d.subtotal = d.price * d.quantity;
                    d.prepTime = sel.prepTime;
                    d.remainingTime = sel.prepTime * d.quantity;

                    f->items[f->itemCount++] = d;
                    f->total += d.subtotal;

                    cout << "Them tiep? (y/n): ";
                    cin >> more; cin.ignore(numeric_limits<streamsize>::max(), '\n');
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
            cout << "Enter ID to temp-bill: ";
            int id;
            cin >> id;
            cin.ignore(numeric_limits<streamsize>::max(), '\n');

            Order *f = findOrderByID(q, id);
            if (!f)
            {
                bool foundPending = false;
                for (int i = 0; i < ListPending.getSize(); ++i)
                    if (ListPending.getValue(i) == id) { foundPending = true; break; }

                if (foundPending)
                {
                    Order &pend = allOrders[id];
                    pend.status = hasCooking(q) ? "Wait" : "Cooking";
                    if (!enqueue(q, pend))
                        cout << "Queue full!\n";
                    else
                    {
                        for (int i = 0; i < ListPending.getSize(); ++i)
                            if (ListPending.getValue(i) == id) { ListPending.removeAt(i); break; }
                        printBill(pend);
                        cout << "[Temp bill] -> " << pend.status << "\n";
                        startTimer(&q);
                    }
                }
                else cout << "Not found.\n";
            }
            else
            {
                f->status = hasCooking(q) ? "Wait" : "Cooking";
                printBill(*f);
                cout << "[Temp bill] -> " << f->status << "\n";
                startTimer(&q);
            }
        }
        else if (act == 4)
        {
            clearScreen();
            continue;
        }
        else if (act == 0) break;
    }
}

// ===================================================================
// Search Customer (naive, tìm ở cả Queue, Pending, Done)
// ===================================================================
void searchCustomer(Queue &q)
{
    if (q.count == 0 && ListPending.getSize() == 0 && ListDone.getSize() == 0)
    {
        cout << "Khong co don nao.\n";
        return;
    }

    cout << "Nhap ten khach hang: ";
    string key;
    getline(cin, key);
    bool found = false;

    for (int i = 0; i < q.count; ++i)
    {
        Order &o = q.orders[(q.front + i) % MAX];
        if (containsNaive(o.customerName, key)) { printOrderRow(o); found = true; }
    }
    for (int i = 0; i < ListPending.getSize(); ++i)
    {
        Order &o = allOrders[ListPending.getValue(i)];
        if (containsNaive(o.customerName, key)) { printOrderRow(o); found = true; }
    }
    for (int i = 0; i < ListDone.getSize(); ++i)
    {
        Order &o = allOrders[ListDone.getValue(i)];
        if (containsNaive(o.customerName, key)) { printOrderRow(o); found = true; }
    }

    if (!found) cout << "Khong tim thay.\n";
}

// ===================================================================
// Timer: tự động giảm 1s/lần, chuyển Done tự động, không in/log
// ===================================================================
void advanceTime(Queue &q, int seconds)
{
    if (seconds <= 0 || q.count == 0) return;

    // tìm đơn đầu tiên đang Cooking
    int targetOffset = -1;
    for (int i = 0; i < q.count; ++i)
    {
        int idx = (q.front + i) % MAX;
        if (q.orders[idx].status == "Cooking") { targetOffset = i; break; }
    }
    if (targetOffset == -1) return;

    int idxT = (q.front + targetOffset) % MAX;
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
        for (int k = targetOffset; k < q.count - 1; ++k)
        {
            int from = (q.front + k + 1) % MAX;
            int to   = (q.front + k) % MAX;
            q.orders[to] = q.orders[from];
        }
        q.rear = (q.rear - 1 + MAX) % MAX;
        q.count--;

        allOrders[doneOrder.id] = doneOrder;
        ListDone.add(doneOrder.id);

        // Promote đơn Wait đầu tiên (nếu có)
        for (int j = 0; j < q.count; ++j)
        {
            int jdx = (q.front + j) % MAX;
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
    g_timerThread.detach(); // đơn giản hoá, không cần join khi thoát
}

// ===================================================================
// Utils: find/remove in Queue, getLastOrderID (robust hơn)
// ===================================================================
Order *findOrderByID(Queue &q, int id)
{
    for (int i = 0; i < q.count; ++i)
    {
        int idx = (q.front + i) % MAX;
        if (q.orders[idx].id == id) return &q.orders[idx];
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
        for (int i = 0; i < q.count; ++i)
            mx = max(mx, q.orders[(q.front + i) % MAX].id);
    }
    // Pending
    for (int i = 0; i < ListPending.getSize(); ++i)
        mx = max(mx, allOrders[ListPending.getValue(i)].id);
    // Done
    for (int i = 0; i < ListDone.getSize(); ++i)
        mx = max(mx, allOrders[ListDone.getValue(i)].id);
    return mx;
}
