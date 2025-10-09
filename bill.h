#pragma once
#include <iostream>
#include <fstream>
#include "Functions.h"
#include "Revenue.h"
using namespace std;

void printBill(const Order &o);
void saveBillToFile(const Order &o);
void payment(Queue &q);

Order *findOrderByID(Queue &q, int id);

bool removeOrderByID(Queue &q, int id, Order &out)
{
    if (isEmpty(q))
        return false;
    int foundIndex = -1;
    // tìm vị trí (offset i trong queue)
    for (int i = 0; i <= q.right; i++)
    {
        if (q.orders[i].id == id)
        {
            foundIndex = i;
            break;
        }
    }
    if (foundIndex == -1)
        return false;

    // sao chép order ra ngoài
    out = q.orders[foundIndex];

    // dịch trái các phần tử sau foundIndex
    for (int i = foundIndex; i < q.right; i++)
    {
        int from = i + 1;
        int to = i;
        q.orders[to] = q.orders[from];
    }
    // cập nhật right
    q.right--;

    // xóa mọi gTableOwner trùng order.id (nếu có merge)
    for (int t = 0; t < NUM_TABLES; t++)
    {
        if (gTableOwner[t] == out.id)
        {
            gTableOwner[t] = 0;
            // nếu không còn order nào cho bàn đó thì chuyển status về Empty
            if (!anyOrderForTable(q, t + 1))
                gTableStatus[t] = "Empty";
        }
    }

    // nếu primary table của out không còn order, cập nhật trạng thái
    if (!anyOrderForTable(q, out.tableNumber))
        gTableStatus[out.tableNumber - 1] = "Empty";

    return true;
}

void printBill(const Order &o)
{
    // Header (ANSI colors)
    cout << "\x1b[36m\n=================================================================\n"
         << "                          HOA DON THANH TOAN           \n"
         << "=================================================================\n\x1b[0m";
    cout << "ID: \x1b[33m" << o.id << "\x1b[0m\n";
    cout << "Khach hang: \x1b[32m" << o.customerName << "\x1b[0m\n";
    cout << "Ban: \x1b[33m" << o.tableNumber << "\x1b[0m\n";
    cout << "-----------------------------------------------------------------\n";
    cout << "\x1b[35mSTT\tTen mon\t\t\tSo luong\tGia\x1b[0m\n";

    for (int i = 0; i < o.itemCount; i++){
        cout << (i + 1) << ".\t" << o.items[i].foodName << "\t\t"
             << o.items[i].quantity << "\t\t" << formatPrice(o.items[i].subtotal) << "\n";
    }

    cout << "-----------------------------------------------------------------\n";
    cout << "\x1b[31mTong cong: " << formatPrice(o.total) << " VND\x1b[0m\n";
    cout << "Trang thai: " << o.status << "\n";

    // Progress section: chỉ 1 dòng tổng thời gian còn lại
    cout << "\n\x1b[33mTien do: \x1b[0m";
    if (o.itemCount == 0)
    {
        cout << "(Khong co mon an)\n";
    }
    else
    {
        if (o.totalRemainingTime > 0)
            cout << o.totalRemainingTime << "s con lai...\n";
        else
            cout << "\x1b[32mDa hoan thanh\x1b[0m\n";
    }

    cout << "\x1b[36m=================================================================\x1b[0m\n";
}

// Ghi bill ra file, KHÔNG còn merge bàn nữa
void saveBillToFile(const Order &o)
{
    ofstream fout("bill.txt", ios::app);
    if (!fout)
    {
        cerr << "Khon the mo bill.txt\n";
        return;
    }

    fout << "=====================================\n";
    fout << "          HOA DON THANH TOAN          \n";
    fout << "=====================================\n";
    fout << "ID:   " << o.id << "\n";
    fout << "Khach hang:  " << o.customerName << "\n";
    fout << "Ban:     " << o.tableNumber << "\n";
    fout << "-------------------------------------\n";
    fout << "Mon        So luong        Gia\n";
    fout << "-------------------------------------\n";
    for (int i = 0; i < o.itemCount; i++)
    {
        fout << o.items[i].foodName << "\t" << o.items[i].quantity << "\t" << o.items[i].subtotal << "\n";
    }
    fout << "-------------------------------------\n";
    fout << "TOTAL: " << o.total << " VND\n";
    fout << "Status: " << o.status << "\n";
    fout << "=====================================\n\n";
    fout.close();
}

void payment(Queue &q)
{
    clearScreen();
    displayTables();

    int tableNo;
    cout << "Nhap so ban can thanh toan: ";
    cin >> tableNo;

    if (tableNo < 1 || tableNo > NUM_TABLES) {
        cout << "Ban khong hop le!\n";
        return;
    }

    // Xac dinh baseId cho ban nay
    int baseId = 0;
    // Nếu có owner thì dùng
    // (gTableOwner được set khi tạo order đầu tiên)
    if (gTableOwner[tableNo - 1] != 0) {
        baseId = gTableOwner[tableNo - 1];
    } else {
        // fallback: lấy id của đơn đầu tiên thuộc bàn này
        for (int i = 0; i <= q.right; i++) {
            Order &o = q.orders[i];
            if (o.tableNumber == tableNo) { baseId = o.id; break; }
        }
    }

    if (baseId == 0) {
        cout << "Khong co don hang nao cho ban nay.\n";
        enter();
        return;
    }

    // Thu thap cac order cung id
    int totalOrders = 0;
    long long previewTotal = 0;
    string customer = "";
    for (int i = 0; i <= q.right; i++) {
        Order &o = q.orders[i];
        if (o.id == baseId) {
            if (customer.empty()) customer = o.customerName;
            previewTotal += o.total;
            totalOrders++;
        }
    }

    if (totalOrders == 0) {
        cout << "Khong tim thay don nao trung ID de thanh toan.\n";
        enter();
        return;
    }

    cout << "\nBan " << tableNo << " gom " << totalOrders
         << " don cung ID #" << baseId
         << " | Khach: " << customer
         << " | Tong du kien: " << formatPrice(previewTotal) << "\n";

    cout << "\nXac nhan thanh toan? (y/n): ";
    char confirm; cin >> confirm;
    if (confirm != 'y' && confirm != 'Y') {
        cout << "Da huy thanh toan.\n";
        enter();
        return;
    }

    // === GỘP CÁC MÓN THEO ID ===
    Order merged;
    merged.id = baseId;
    merged.customerName = customer;
    merged.tableNumber = tableNo;
    merged.status = "Completed";
    merged.itemCount = 0;
    merged.total = 0;
    merged.totalRemainingTime = 0;

    // Gộp items (hạn chế MAX_ITEMS: gộp trùng tên món)
    for (int i = 0; i <= q.right; i++) {
        Order &o = q.orders[i];
        if (o.id != baseId) continue;

        for (int j = 0; j < o.itemCount; j++) {
            const OrderDetail &it = o.items[j];

            // Tìm xem món đã có trong merged chưa
            int pos = -1;
            for (int k = 0; k < merged.itemCount; k++) {
                if (merged.items[k].foodName == it.foodName) {
                    pos = k; break;
                }
            }
            if (pos != -1) {
                merged.items[pos].quantity += it.quantity;
                merged.items[pos].subtotal += it.subtotal;
                // giữ nguyên price, prepTime; remainingTime không cần khi Completed
            } else {
                if (merged.itemCount < MAX_ITEMS) {
                    merged.items[merged.itemCount] = it;
                    // remainingTime không dùng cho bill đã thanh toán
                    merged.items[merged.itemCount].remainingTime = 0;
                    merged.itemCount++;
                } else {
                    // nếu vượt MAX_ITEMS thì cộng dồn vào món cuối cùng (an toàn)
                    merged.items[merged.itemCount - 1].quantity += it.quantity;
                    merged.items[merged.itemCount - 1].subtotal += it.subtotal;
                }
            }
            merged.total += it.subtotal;
        }
    }

    // In & lưu & doanh thu
    printBill(merged);
    saveBillToFile(merged);
    updateRevenue(merged);
    cout << "\x1b[32mThanh toan thanh cong!\x1b[0m\n";

    // Xoa TAT CA order co id = baseId
    while (true) {
        Order *p = findOrderByID(q, baseId);
        if (!p) break;
        Order removed;
        removeOrderByID(q, baseId, removed);
    }

    // Giai phong ban neu khong con order nao
    if (!anyOrderForTable(q, tableNo))
        gTableStatus[tableNo - 1] = "Empty";

    cin.ignore();
    enter();
}

