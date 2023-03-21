package com.celonis.invoicechecker;

import lombok.*;
import org.json.JSONArray;
import org.json.JSONObject;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.*;

public class DateTimeMatcher {
    private static final SimpleDateFormat simpleDateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");

    public static DateTime preprocess(DateTime dateTime) {
        try {
            Date parsedDate = simpleDateFormat.parse(dateTime.getDateTimeStr());
            Calendar calendar = new GregorianCalendar();
            calendar.setTime(parsedDate);
            if (calendar.get(Calendar.YEAR) < 1970 || calendar.get(Calendar.YEAR) >= 2040) {
                return null;
            }
            dateTime.setDateTime(parsedDate);
            dateTime.setCalendar(getCalendarFromDateTime(parsedDate));
        } catch (ParseException e) {
            // We don't need to terminate the program if the parsing fails. We simply ignore the ill-format.
            e.printStackTrace();
            return null;
        }
        return dateTime;
    }

    private static Calendar getCalendarFromDateTime(Date dateTime) {
        Calendar calendar = new GregorianCalendar();
        calendar.setTime(dateTime);
        calendar.set(Calendar.HOUR_OF_DAY, 0);
        calendar.set(Calendar.MINUTE, 0);
        calendar.set(Calendar.SECOND, 0);

        return calendar;
    }

    // We assume that `input` is the string representation of a json object.
    // { "c" : [{"id": 1, "date_time": "2020-01-01 12:04:03"}, {"id": 2, "date_time": "2020-01-02 12:03:02"}]}
    // We output the string representation of a json object.
    // { "c": [{"c": [{"id": 1, "date_time": "2020-01-01 12:04:03"}]}]}
    public final String[] process(String input) {
        JSONObject inputObjects = new JSONObject(input);
        List<ClusterObjectInterface> dateTimes = new LinkedList<>();
        JSONArray jsonArray = inputObjects.getJSONArray("c");
        for (int i = 0; i < jsonArray.length(); i++) {
            JSONObject obj = jsonArray.getJSONObject(i);
            DateTime dateTime = new DateTime(obj.getString("id"), obj.getString("date_time"));
            DateTime processedDateTime = preprocess(dateTime);
            if (processedDateTime != null) {
                dateTimes.add(processedDateTime);
            }
        }

        DateTimeIndexer indexer = new DateTimeIndexer();
        indexer.index(dateTimes);
        IndexCluster indexCluster = new IndexCluster(indexer);
        return indexCluster.cluster(dateTimes);
    }

    @RequiredArgsConstructor
    @EqualsAndHashCode
    @ToString
    static class DateTime implements ClusterObjectInterface {
        @Getter
        private final String rowId;

        @Getter
        private final String dateTimeStr;

        @Setter
        private int numDays = 7;

        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private Date dateTime;

        @Setter
        @Getter
        @EqualsAndHashCode.Exclude
        private Calendar calendar;

        public String toJsonString() {
            return "{\"id\": \"" + rowId + "\", \"date_time\": \"" + dateTimeStr + "\"}";
        }

        public boolean isSimilar(ClusterObjectInterface other) {
            DateTime otherDateTime = (DateTime) other;
            Calendar currentCalendar = getCalendar();
            Calendar otherCalendar = otherDateTime.getCalendar();

            if (currentCalendar.get(Calendar.YEAR) == otherCalendar.get(Calendar.YEAR) &&
                    currentCalendar.get(Calendar.DAY_OF_YEAR) == otherCalendar.get(Calendar.DAY_OF_YEAR)) {
                return true;
            }

            // If time diff is less than 7 days, we match these two dateTime.
            if (Math.abs(currentCalendar.getTimeInMillis() - otherCalendar.getTimeInMillis())
                    <= numDays * 24 * 60 * 60 * 1000) {
                return true;
            }

            if (currentCalendar.get(Calendar.YEAR) != otherCalendar.get(Calendar.YEAR)) {
                return false;
            }

            // If month and date are swapped, we match these two dateTime.
            // Note that get(Calendar.MONTH) is zero-based.
            if (currentCalendar.get(Calendar.MONTH) + 1 == otherCalendar.get(Calendar.DAY_OF_MONTH) &&
                    currentCalendar.get(Calendar.DAY_OF_MONTH) == otherCalendar.get(Calendar.MONTH) + 1) {
                return true;
            }

            if (currentCalendar.get(Calendar.DAY_OF_MONTH) != otherCalendar.get(Calendar.DAY_OF_MONTH)) {
                return false;
            }

            // Note that get(Calendar.MONTH) is zero-based.
            if ((currentCalendar.get(Calendar.MONTH) + 1 == 6 || currentCalendar.get(Calendar.MONTH) + 1 == 7) &&
                    (otherCalendar.get(Calendar.MONTH) + 1 == 6 || otherCalendar.get(Calendar.MONTH) + 1 == 7)) {
                return true;
            }

            if ((currentCalendar.get(Calendar.MONTH) + 1 == 9 || currentCalendar.get(Calendar.MONTH) + 1 == 10) &&
                    (otherCalendar.get(Calendar.MONTH) + 1 == 9 || otherCalendar.get(Calendar.MONTH) + 1 == 10)) {
                return true;
            }

            return false;
        }

        public String getId() {
            return rowId;
        }
    }
}
