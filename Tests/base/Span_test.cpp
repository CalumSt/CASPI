#pragma once
#include <gtest/gtest.h>
#include "core/caspi_Span.h"  // Your Span/StridedSpan/SpanView header

using namespace CASPI::Core;

// ----------------------
// Unit Tests for Span
// ----------------------
TEST(SpanTest, BasicContiguousAccess) {
    int data[] = {1, 2, 3, 4};
    Span<int> span(data, 4);

    EXPECT_EQ(span.size(), 4u);
    EXPECT_FALSE(span.empty());

    for (std::size_t i = 0; i < span.size(); ++i)
        EXPECT_EQ(span[i], static_cast<int>(i+1));

    int sum = 0;
    for (auto v: span) sum += v;
    EXPECT_EQ(sum, 10);
}

// ----------------------
// Unit Tests for StridedSpan
// ----------------------
TEST(StridedSpanTest, BasicStridedAccess) {
    int data[] = {1, 10, 2, 20, 3, 30};
    StridedSpan<int> span(data, 3, 2); // Every 2 elements

    EXPECT_EQ(span.size(), 3u);
    EXPECT_EQ(span[0], 1);
    EXPECT_EQ(span[1], 2);
    EXPECT_EQ(span[2], 3);

    int sum = 0;
    for (auto v: span) sum += v;
    EXPECT_EQ(sum, 6);
}

// ----------------------
// Unit Tests for SpanView
// ----------------------
TEST(SpanViewTest, ContiguousView) {
    int data[] = {5, 6, 7};
    SpanView<int> view(data, 3); // Contiguous
    EXPECT_EQ(view.size(), 3u);
    EXPECT_EQ(view.type(), SpanView<int>::Type::Contiguous);

    int sum = 0;
    for (auto v: view) sum += v;
    EXPECT_EQ(sum, 18);
}

TEST(SpanViewTest, StridedView) {
    int data[] = {1, 100, 2, 200, 3, 300};
    SpanView<int> view(data, 3, 2); // Strided every 2 elements
    EXPECT_EQ(view.size(), 3u);
    EXPECT_EQ(view.type(), SpanView<int>::Type::Strided);

    int sum = 0;
    for (auto v: view) sum += v;
    EXPECT_EQ(sum, 6);
}

// ----------------------
// Edge Cases
// ----------------------
TEST(SpanViewTest, EmptySpan) {
    int *ptr = nullptr;
    Span<int> span(ptr, 0);
    EXPECT_TRUE(span.empty());
    EXPECT_EQ(span.size(), 0u);

    StridedSpan<int> sspan(ptr, 0, 1);
    EXPECT_EQ(sspan.size(), 0u);

    SpanView<int> view(ptr, 0);
    EXPECT_EQ(view.size(), 0u);
}

// Optional: iterator comparison
TEST(StridedSpanTest, IteratorComparison) {
    int data[] = {1, 2, 3, 4, 5, 6};
    StridedSpan<int> span(data, 3, 2);
    auto it = span.begin();
    auto end = span.end();
    EXPECT_NE(it, end);
    ++it;
    ++it;
    ++it;
    EXPECT_EQ(it, end);
}

#include <numeric> // std::accumulate

TEST(SpanTest, ContiguousIterateAndSTL) {
    int data[] = {1, 2, 3, 4};
    Span<int> span(data, 4);
    int sum = std::accumulate(span.begin(), span.end(), 0);
    EXPECT_EQ(sum, 10);

    std::sort(span.begin(), span.end(), std::greater<int>());
    EXPECT_EQ(span[0], 4);
    EXPECT_EQ(span[3], 1);
}

TEST(StridedSpanTest, IterateAndAccumulate) {
    int data[] = {1, 100, 2, 200, 3, 300};
    StridedSpan<int> sspan(data, 3, 2);
    int sum = std::accumulate(sspan.begin(), sspan.end(), 0);
    EXPECT_EQ(sum, 6);
}

TEST(SpanViewTest, ContiguousAndSTL) {
    int data[] = {5, 6, 7};
    SpanView<int> view(data, 3);
    EXPECT_EQ(std::accumulate(view.begin(), view.end(), 0), 18);
}

TEST(SpanViewTest, StridedAndSTL) {
    int data[] = {1, 100, 2, 200, 3, 300};
    SpanView<int> view(data, 3, 2);
    EXPECT_EQ(std::accumulate(view.begin(), view.end(), 0), 6);
}

TEST(SpanViewTest, EmptySpan) {
    int *ptr = nullptr;
    Span<int> s(ptr, 0);
    EXPECT_TRUE(s.empty());
    StridedSpan<int> ss(ptr, 0, 1);
    EXPECT_EQ(ss.size(), 0);
    SpanView<int> sv(ptr, 0);
    EXPECT_EQ(sv.size(), 0);
}
